#include "scratch/float_lower.hpp"
#include <algorithm>
#include <cstdint>
#include <set>
#include <utility>

namespace scratch {
namespace {

Json int_type(unsigned bits) {
    unsigned bytes = (bits + 7) / 8;
    unsigned align = 1;
    while (align < bytes && align < 8) align *= 2;
    return {{"kind", "int"}, {"bits", bits}, {"size", bytes},
            {"alloc_size", ((bytes + align - 1) / align) * align}, {"align", align}};
}

Json constant(unsigned bits, uint64_t value) {
    Json bytes = Json::array();
    for (unsigned i = 0; i < (bits + 7) / 8; ++i)
        bytes.push_back(static_cast<unsigned>((value >> (i * 8)) & 255));
    return {{"kind", "bytes"}, {"type", int_type(bits)}, {"bytes", bytes}};
}

std::string operation(const Json& instruction) {
    const std::string opcode = instruction.value("op", "");
    static const std::set<std::string> direct = {
        "fadd", "fsub", "fmul", "fdiv", "frem", "fneg", "fcmp",
        "sitofp", "uitofp", "fptosi", "fptoui", "fptrunc", "fpext"};
    if (direct.count(opcode)) return opcode;
    if (opcode != "call" || !instruction.contains("callee")) return {};
    const auto& callee = instruction.at("callee");
    if (callee.value("kind", "") != "symbol") return {};
    const auto name = callee.value("name", "");
    static const std::vector<std::string> intrinsic = {
        "fma", "fmuladd", "sqrt", "fabs", "copysign", "minnum", "maxnum",
        "minimum", "maximum", "minimumnum", "maximumnum",
        "ceil", "floor", "trunc", "round", "roundeven", "rint", "nearbyint"};
    for (const auto& kind : intrinsic)
        if (name.rfind("llvm." + kind + ".", 0) == 0) return kind;
    return {};
}

class FloatLowering {
    Json module_;
    Json* output_ = nullptr;
    Json pointer_;
    std::set<std::string> used_ids_;
    std::set<std::string> dependencies_;
    uint64_t serial_ = 0;
    std::string function_, block_, instruction_;

    [[noreturn]] void fail(const std::string& message) const {
        throw Error("function '" + function_ + "', block '" + block_ +
                    "', instruction '" + instruction_ + "': " + message);
    }

    std::string fresh() {
        std::string name;
        do { name = "__fp" + std::to_string(serial_++); } while (used_ids_.count(name));
        used_ids_.insert(name);
        return name;
    }

    Json emit(const std::string& op, const Json& type, const Json& operands,
              Json extra = Json::object(), std::string id = {}) {
        if (id.empty()) id = fresh();
        Json inst = {{"id", id}, {"op", op}, {"type", type}, {"operands", operands}};
        for (auto it = extra.begin(); it != extra.end(); ++it) inst[it.key()] = it.value();
        output_->push_back(std::move(inst));
        return {{"kind", "ref"}, {"id", id}, {"type", type}};
    }

    Json call(const std::string& name, unsigned bits, const Json& args) {
        dependencies_.insert(name);
        Json attributes = Json::array();
        for (size_t i = 0; i < args.size(); ++i) attributes.push_back(Json::object());
        return emit("call", int_type(bits), args,
                    {{"callee", {{"kind", "symbol"}, {"name", name},
                                 {"symbol_kind", "function"}, {"addend", 0}, {"type", pointer_}}},
                     {"arg_attrs", attributes}, {"attrs", Json::object()},
                     {"return_attrs", Json::object()}, {"tail_kind", "none"},
                     {"tail", false}, {"vararg", false}, {"calling_convention", 0}});
    }

    unsigned float_bits(const Json& type) const {
        const auto bits = type.value("bits", 0u);
        if (type.value("kind", "") != "float" || (bits != 32 && bits != 64))
            fail("floating lowering supports IEEE float/double (32/64 bits), not " + type.dump());
        return bits;
    }

    Json bit_pattern(const Json& operand) {
        unsigned bits = float_bits(operand.at("type"));
        // Constants already carry exact bytes; retyping them is a constant
        // bitcast and avoids a redundant runtime instruction.
        if (operand.value("kind", "") == "bytes" || operand.value("kind", "") == "zero" ||
            operand.value("kind", "") == "undef" || operand.value("kind", "") == "poison") {
            Json result = operand;
            result["type"] = int_type(bits);
            return result;
        }
        return emit("bitcast", int_type(bits), Json::array({operand}));
    }

    Json binary(const std::string& op, const Json& a, const Json& b) {
        return emit(op, a.at("type"), Json::array({a, b}));
    }

    Json negate_bool(const Json& a) { return binary("xor", a, constant(1, 1)); }

    Json boolean_call(const std::string& name, const Json& args) {
        auto result = call(name, 32, args);
        return emit("trunc", int_type(1), Json::array({result}));
    }

    Json choose(const Json& condition, const Json& yes, const Json& no) {
        return emit("select", yes.at("type"), Json::array({condition, yes, no}));
    }

    Json comparison(const std::string& predicate, unsigned bits, const Json& a, const Json& b) {
        auto cmp = [&](const std::string& op, bool reverse = false) {
            return boolean_call("__sclr_f" + std::to_string(bits) + "_" + op,
                                reverse ? Json::array({b, a}) : Json::array({a, b}));
        };
        auto unordered = [&] {
            const auto name = "__sclr_f" + std::to_string(bits) + "_isnan";
            auto aa = boolean_call(name, Json::array({a}));
            auto bb = boolean_call(name, Json::array({b}));
            return binary("or", aa, bb);
        };
        if (predicate == "false") return constant(1, 0);
        if (predicate == "true") return constant(1, 1);
        if (predicate == "oeq") return cmp("eq");
        if (predicate == "olt") return cmp("lt");
        if (predicate == "ole") return cmp("le");
        if (predicate == "ogt") return cmp("lt", true);
        if (predicate == "oge") return cmp("le", true);
        if (predicate == "uno") return unordered();
        if (predicate == "ord") return negate_bool(unordered());
        if (predicate == "une") return negate_bool(cmp("eq"));
        if (predicate == "ugt") return negate_bool(cmp("le"));
        if (predicate == "uge") return negate_bool(cmp("lt"));
        if (predicate == "ult") return negate_bool(cmp("le", true));
        if (predicate == "ule") return negate_bool(cmp("lt", true));
        if (predicate == "ueq") {
            auto equal = cmp("eq");
            return binary("or", equal, unordered());
        }
        if (predicate == "one") {
            auto different = negate_bool(cmp("eq"));
            return binary("and", different, negate_bool(unordered()));
        }
        fail("unknown fcmp predicate '" + predicate + "'");
    }

    Json minmax(const std::string& op, unsigned bits, const Json& a, const Json& b) {
        const bool minimum = op.rfind("min", 0) == 0;
        const bool propagate = op == "minimum" || op == "maximum";
        auto less = comparison("olt", bits, a, b);
        auto equal = comparison("oeq", bits, a, b);
        auto unequal = minimum ? choose(less, a, b) : choose(less, b, a);
        // Equal nonzero numbers have identical encodings. For +/-0, OR picks
        // -0 for minimum and AND picks +0 for maximum, in either argument order.
        auto tied = binary(minimum ? "or" : "and", a, b);
        auto numeric = choose(equal, tied, unequal);
        const auto name = "__sclr_f" + std::to_string(bits) + "_isnan";
        auto a_nan = boolean_call(name, Json::array({a}));
        auto b_nan = boolean_call(name, Json::array({b}));
        auto qnan = constant(bits, bits == 32 ? UINT64_C(0x7fc00000) : UINT64_C(0x7ff8000000000000));
        if (propagate) return choose(binary("or", a_nan, b_nan), qnan, numeric);
        // minnum/maxnum permit treating sNaN as qNaN. minimumnum/maximumnum
        // require that choice. Both-NaN results are explicitly quieted.
        auto when_a_nan = choose(b_nan, qnan, b);
        auto when_a_number = choose(b_nan, a, numeric);
        return choose(a_nan, when_a_nan, when_a_number);
    }

    Json scalar(const Json& instruction, const std::string& op, const Json& args, const Json& result_type) {
        if (op == "sitofp" || op == "uitofp") {
            unsigned bits = float_bits(result_type);
            unsigned source = args.at(0).at("type").at("bits");
            if (args.at(0).at("type").value("kind", "") != "int" || !source || source > 64)
                fail("integer-to-float conversion requires an integer width from 1 through 64; refusing lossy truncation");
            unsigned promoted = source <= 32 ? 32 : 64;
            Json argument = args.at(0);
            if (source != promoted)
                argument = emit(op == "sitofp" ? "sext" : "zext", int_type(promoted), Json::array({argument}));
            const std::string prefix = op == "sitofp" ? "i" : "u";
            auto raw = call("__sclr_" + prefix + std::to_string(promoted) + "_to_f" + std::to_string(bits),
                            bits, Json::array({argument}));
            return emit("bitcast", result_type, Json::array({raw}));
        }

        if (args.empty()) fail("floating operation without operands");
        unsigned bits = float_bits(args.at(0).at("type"));
        auto a = bit_pattern(args.at(0));
        if (op == "fptosi" || op == "fptoui") {
            unsigned destination = result_type.value("bits", 0u);
            if (result_type.value("kind", "") != "int" || !destination || destination > 64)
                fail("float-to-integer conversion requires an integer width from 1 through 64; refusing lossy truncation");
            unsigned promoted = destination <= 32 ? 32 : 64;
            auto raw = call("__sclr_f" + std::to_string(bits) + "_to_" + (op == "fptosi" ? "i" : "u") +
                            std::to_string(promoted), promoted, Json::array({a}));
            if (destination != promoted) return emit("trunc", result_type, Json::array({raw}));
            return raw;
        }
        if (op == "fptrunc" || op == "fpext") {
            unsigned destination = float_bits(result_type);
            if ((op == "fptrunc" && destination >= bits) || (op == "fpext" && destination <= bits))
                fail("invalid floating extension/truncation direction");
            auto raw = call("__sclr_f" + std::to_string(bits) + "_to_f" + std::to_string(destination),
                            destination, Json::array({a}));
            return emit("bitcast", result_type, Json::array({raw}));
        }

        if (op == "fneg" || op == "fabs") {
            uint64_t sign = UINT64_C(1) << (bits - 1);
            auto mask = constant(bits, op == "fneg" ? sign : sign - 1);
            auto raw = binary(op == "fneg" ? "xor" : "and", a, mask);
            return emit("bitcast", result_type, Json::array({raw}));
        }

        static const std::set<std::string> unary = {
            "sqrt", "ceil", "floor", "trunc", "round", "roundeven", "rint", "nearbyint"};
        if (unary.count(op)) {
            // Ordinary LLVM rint/nearbyint assume the default floating-point
            // environment: nearest, ties to even; status flags are unobservable.
            // Constrained intrinsics must retain their separate rejection path.
            const auto runtime_op = op == "rint" || op == "nearbyint" ? "roundeven" : op;
            auto raw = call("__sclr_f" + std::to_string(bits) + "_" + runtime_op, bits, Json::array({a}));
            return emit("bitcast", result_type, Json::array({raw}));
        }
        if (args.size() < 2 || float_bits(args.at(1).at("type")) != bits)
            fail("floating binary operation has incompatible operands");
        auto b = bit_pattern(args.at(1));
        if (op == "fcmp") return comparison(instruction.at("predicate"), bits, a, b);

        Json raw;
        if (op == "copysign") {
            uint64_t sign = UINT64_C(1) << (bits - 1);
            auto magnitude = binary("and", a, constant(bits, sign - 1));
            auto sign_bits = binary("and", b, constant(bits, sign));
            raw = binary("or", magnitude, sign_bits);
        } else if (op == "fma" || op == "fmuladd") {
            if (args.size() != 3 || float_bits(args.at(2).at("type")) != bits)
                fail("floating fused operation requires three matching operands");
            auto c = bit_pattern(args.at(2));
            raw = call("__sclr_f" + std::to_string(bits) + "_fma", bits, Json::array({a, b, c}));
        } else if (op == "minnum" || op == "maxnum" || op == "minimum" || op == "maximum" ||
                   op == "minimumnum" || op == "maximumnum") {
            raw = minmax(op, bits, a, b);
        } else {
            static const std::set<std::string> arithmetic = {"fadd", "fsub", "fmul", "fdiv", "frem"};
            if (!arithmetic.count(op)) fail("unsupported floating operation '" + op + "'");
            raw = call("__sclr_f" + std::to_string(bits) + "_" + op.substr(1), bits, Json::array({a, b}));
        }
        return emit("bitcast", result_type, Json::array({raw}));
    }

    void lower(const Json& instruction, const std::string& op) {
        if (instruction.value("tail_kind", "none") == "musttail")
            fail("musttail floating intrinsic requires tail-transfer lowering; cannot silently replace its call contract");
        const auto& type = instruction.at("type");
        const auto& args = instruction.at("operands");
        if (type.value("kind", "") != "vector") {
            auto value = scalar(instruction, op, args, type);
            finish(instruction, value);
            return;
        }
        const unsigned count = type.at("count");
        const Json& element = type.at("element");
        Json aggregate = {{"kind", "zero"}, {"type", type}};
        for (unsigned lane = 0; lane < count; ++lane) {
            Json scalar_args = Json::array();
            for (const auto& argument : args) {
                const auto& input_type = argument.at("type");
                if (input_type.value("kind", "") != "vector" || input_type.at("count") != count)
                    fail("elementwise floating vector operation requires matching lane counts");
                scalar_args.push_back(emit("extractelement", input_type.at("element"),
                                          Json::array({argument, constant(32, lane)})));
            }
            auto result = scalar(instruction, op, scalar_args, element);
            aggregate = emit("insertelement", type, Json::array({aggregate, result, constant(32, lane)}));
        }
        finish(instruction, aggregate);
    }

    void finish(const Json& instruction, const Json& value) {
        // Reuse the final result slot where possible rather than introducing
        // an extra whole-value copy. It has not yet escaped this expansion.
        if (!output_->empty() && value.value("kind", "") == "ref" &&
            output_->back().at("id") == value.at("id") &&
            output_->back().at("type") == instruction.at("type")) {
            output_->back()["id"] = instruction.at("id");
        } else {
            emit("bitcast", instruction.at("type"), Json::array({value}), Json::object(), instruction.at("id"));
        }
    }

public:
    explicit FloatLowering(Json module) : module_(std::move(module)) {
        unsigned bytes = module_.value("pointer_bytes", 8u);
        pointer_ = {{"kind", "pointer"}, {"bits", bytes * 8}, {"size", bytes},
                    {"alloc_size", bytes}, {"align", bytes}, {"address_space", 0},
                    {"index_bits", module_.value("pointer_index_bits", bytes * 8)}};
        for (const auto& function : module_.at("functions")) {
            for (const auto& argument : function.at("args")) used_ids_.insert(argument.at("id"));
            for (const auto& block : function.at("blocks"))
                for (const auto& instruction : block.at("instructions")) used_ids_.insert(instruction.at("id"));
        }
    }

    Json run() {
        for (auto& function : module_.at("functions")) {
            function_ = function.at("name");
            for (auto& block : function.at("blocks")) {
                block_ = block.at("id");
                Json output = Json::array();
                output_ = &output;
                for (const auto& instruction : block.at("instructions")) {
                    instruction_ = instruction.at("id");
                    const auto op = operation(instruction);
                    if (op.empty()) output.push_back(instruction);
                    else {
                        const auto first = output.size();
                        lower(instruction, op);
                        // A lowered float instruction is one source operation.
                        // Stop before its expansion, not halfway through a
                        // SoftFloat call or its final result copy.
                        if (output.size() > first) {
                            if (instruction.contains("debug")) output[first]["debug"] = instruction.at("debug");
                            if (function.contains("debug_variables")) {
                                for (auto& variable : function["debug_variables"])
                                    for (auto& location : variable["locations"])
                                        if (location.value("block", "") == block_ &&
                                            location.value("before", "") == instruction_)
                                            location["before"] = output[first].at("id");
                            }
                        }
                    }
                }
                block["instructions"] = std::move(output);
            }
        }
        return std::move(module_);
    }

    std::vector<std::string> dependencies() const { return {dependencies_.begin(), dependencies_.end()}; }
};

} // namespace

std::vector<std::string> floating_dependencies(const Json& module) {
    FloatLowering lower(module);
    lower.run();
    return lower.dependencies();
}

Json lower_floating(Json module) { return FloatLowering(std::move(module)).run(); }

} // namespace scratch
