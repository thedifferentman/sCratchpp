#include "scratch/slot_numeric.hpp"

namespace scratch {
namespace {
constexpr const char* memory = "__scl_memory";

unsigned bytes_for(unsigned bits) {
    if (!bits) throw Error("zero-width integer is not supported");
    return (bits - 1) / 8 + 1;
}

Bytes read_slot(Expr address, unsigned bytes) {
    Bytes result;
    for (unsigned i = 0; i < bytes; ++i)
        result.push_back(item(memory, i ? add(address, i) : address));
    return result;
}

void write_slot(Script& code, Expr address, const Bytes& bytes) {
    for (std::size_t i = 0; i < bytes.size(); ++i)
        code.push_back(replace(memory, i ? add(address, i) : address, bytes[i]));
}

std::string operand_parameter(std::size_t index) {
    return "A" + std::to_string(index);
}

void assign_registers(Script& code, const Bytes& registers, const Bytes& values) {
    if (registers.size() != values.size()) throw Error("numeric division register width mismatch");
    for (std::size_t i = 0; i < registers.size(); ++i)
        code.push_back(set(registers[i].at("fields").at("VARIABLE").at(0).get<std::string>(), values[i]));
}
}

SlotNumeric::SlotNumeric(Project& project) : project_(project) {}

std::string SlotNumeric::temporary(const std::string& hint) {
    return "__slot_numeric_" + hint + "_" + std::to_string(counter_++);
}

Stmt SlotNumeric::operation(const std::string& signature, Expr destination,
        const std::vector<Expr>& source_slots, const std::vector<unsigned>& byte_lengths,
        const std::function<NumericResult(const std::vector<Bytes>&)>& generate) {
    if (source_slots.size() != byte_lengths.size())
        throw Error("slot numeric source and byte-length counts differ");
    std::string key = signature;
    for (unsigned bytes : byte_lengths) {
        if (!bytes) throw Error("slot numeric operand has no bytes");
        key += "|" + std::to_string(bytes);
    }
    auto found = helpers_.find(key);
    if (found == helpers_.end()) {
        auto name = temporary("helper");
        std::vector<std::string> parameters{"D"};
        std::vector<Bytes> operands;
        for (std::size_t i = 0; i < byte_lengths.size(); ++i) {
            auto parameter = operand_parameter(i);
            parameters.push_back(parameter);
            operands.push_back(read_slot(arg(parameter), byte_lengths[i]));
        }
        // Numeric snapshots its input reporters and materializes its result. No
        // memory writes occur until the entire operation has completed, making
        // both same-slot and partially overlapping source/destination safe.
        auto generated = generate(operands);
        write_slot(generated.code, arg("D"), generated.bytes);
        project_.procedure(name, parameters, std::move(generated.code));
        found = helpers_.emplace(std::move(key), std::move(name)).first;
    }
    std::vector<Expr> arguments{std::move(destination)};
    arguments.insert(arguments.end(), source_slots.begin(), source_slots.end());
    return call(found->second, arguments);
}

const SlotNumeric::Division& SlotNumeric::unsigned_division_core(unsigned bits) {
    std::string key = "core|" + std::to_string(bits);
    auto found = divisions_.find(key);
    if (found == divisions_.end()) {
        Division helper;
        helper.name = temporary("udivrem_core");
        unsigned bytes = bytes_for(bits);
        for (unsigned i = 0; i < bytes; ++i) {
            helper.lhs.push_back(var(temporary("division_lhs")));
            helper.rhs.push_back(var(temporary("division_rhs")));
        }
        // These private work registers allow signed and unsigned adapters to
        // reuse one quotient/remainder algorithm without allocating guest memory
        // or packing individual bytes at any LLVM instruction call site.
        auto generated = numeric_.divrem(bits, helper.lhs, helper.rhs);
        helper.quotient.assign(generated.bytes.begin(), generated.bytes.begin() + bytes);
        helper.remainder.assign(generated.bytes.begin() + bytes, generated.bytes.end());
        project_.procedure(helper.name, {}, std::move(generated.code));
        found = divisions_.emplace(std::move(key), std::move(helper)).first;
    }
    return found->second;
}

const SlotNumeric::Division& SlotNumeric::division(unsigned bits, bool signed_op) {
    std::string key = std::string(signed_op ? "s|" : "u|") + std::to_string(bits);
    auto found = divisions_.find(key);
    if (found == divisions_.end()) {
        const auto& core = unsigned_division_core(bits);
        Division helper;
        helper.name = temporary(signed_op ? "sdivrem" : "udivrem");
        helper.quotient = core.quotient;
        helper.remainder = core.remainder;
        unsigned bytes = bytes_for(bits);
        Bytes lhs = read_slot(arg("A"), bytes), rhs = read_slot(arg("B"), bytes);
        Script code;
        Expr lhs_sign = 0, rhs_sign = 0;
        if (signed_op) {
            auto lhs_sign_name = temporary("division_lhs_sign");
            auto rhs_sign_name = temporary("division_rhs_sign");
            unsigned top_bits = (bits - 1) % 8 + 1;
            auto sign = [&](const Bytes& source) {
                return floor_(scratch::div(mod(source.back(), 1u << top_bits), 1u << (top_bits - 1)));
            };
            code.push_back(set(lhs_sign_name, sign(lhs)));
            code.push_back(set(rhs_sign_name, sign(rhs)));
            lhs_sign = var(lhs_sign_name);
            rhs_sign = var(rhs_sign_name);
            auto magnitude_a = numeric_.intrinsic("abs", bits, {lhs});
            auto magnitude_b = numeric_.intrinsic("abs", bits, {rhs});
            extend(code, magnitude_a.code);
            extend(code, magnitude_b.code);
            lhs = std::move(magnitude_a.bytes);
            rhs = std::move(magnitude_b.bytes);
        }
        assign_registers(code, core.lhs, lhs);
        assign_registers(code, core.rhs, rhs);
        code.push_back(call(core.name, {}));
        if (signed_op) {
            auto negate = [&](const Bytes& output) {
                auto generated = numeric_.binary("sub", bits, Bytes(bytes, Expr(0)), output);
                assign_registers(generated.code, output, generated.bytes);
                return generated.code;
            };
            code.push_back(iff(lnot(eq(lhs_sign, rhs_sign)), negate(helper.quotient)));
            code.push_back(iff(eq(lhs_sign, 1), negate(helper.remainder)));
        }
        // Outputs remain live until the public udiv/urem/sdiv/srem adapter
        // commits them to D. No user function can run during this sequence.
        project_.procedure(helper.name, {"A", "B"}, std::move(code));
        found = divisions_.emplace(std::move(key), std::move(helper)).first;
    }
    return found->second;
}

Stmt SlotNumeric::binary(const std::string& op, unsigned bits, Expr destination, Expr lhs, Expr rhs) {
    unsigned bytes = bytes_for(bits);
    if (op == "udiv" || op == "urem" || op == "sdiv" || op == "srem") {
        return operation("binary|" + op + "|" + std::to_string(bits),
            std::move(destination), {std::move(lhs), std::move(rhs)}, {bytes, bytes},
            [&](const std::vector<Bytes>&) {
                const auto& helper = division(bits, op[0] == 's');
                return NumericResult{{call(helper.name, {arg("A0"), arg("A1")})},
                    op.substr(1) == "div" ? helper.quotient : helper.remainder};
            });
    }
    return operation("binary|" + op + "|" + std::to_string(bits),
        std::move(destination), {std::move(lhs), std::move(rhs)}, {bytes, bytes},
        [&](const std::vector<Bytes>& a) { return numeric_.binary(op, bits, a[0], a[1]); });
}

Stmt SlotNumeric::shift_constant(const std::string& op, unsigned bits, unsigned amount,
        Expr destination, Expr source) {
    if (op != "shl" && op != "lshr" && op != "ashr")
        throw Error("unsupported constant integer shift: " + op);
    const unsigned bytes = bytes_for(bits);
    return operation("shift_constant|" + op + "|" + std::to_string(bits) + "|" + std::to_string(amount),
        std::move(destination), {std::move(source)}, {bytes},
        [&](const std::vector<Bytes>& operands) {
            NumericResult generated;
            if (amount >= bits) {
                generated.bytes.assign(bytes, Expr(0));
                return generated;
            }
            const unsigned top_bits = (bits - 1) % 8 + 1;
            const unsigned top_modulus = 1u << top_bits;
            Bytes input;
            for (unsigned i = 0; i < bytes; ++i) {
                auto name = temporary("constant_shift_input");
                generated.code.push_back(set(name, i + 1 == bytes
                    ? mod(operands[0][i], top_modulus) : operands[0][i]));
                input.push_back(var(name));
            }
            Expr fill = 0;
            if (op == "ashr") {
                auto sign_name = temporary("constant_shift_sign");
                generated.code.push_back(set(sign_name,
                    floor_(scratch::div(input.back(), 1u << (top_bits - 1)))));
                fill = mul(var(sign_name), 255);
                // Sign-extend the partial high byte before treating the input as
                // an ordinary byte stream. Bytes beyond the input use the same
                // sign fill; the final destination is masked back to exactly iN.
                if (top_bits != 8) {
                    auto top_name = temporary("constant_shift_top");
                    generated.code.push_back(set(top_name,
                        add(input.back(), mul(var(sign_name), 256 - top_modulus))));
                    input.back() = var(top_name);
                }
            }
            const unsigned whole_bytes = amount / 8;
            const unsigned residual_bits = amount % 8;
            auto right_byte = [&](unsigned i) -> Expr { return i < bytes ? input[i] : fill; };
            for (unsigned i = 0; i < bytes; ++i) {
                Expr value = 0;
                if (op == "shl") {
                    if (i >= whole_bytes) {
                        const unsigned j = i - whole_bytes;
                        value = input[j];
                        if (residual_bits) {
                            value = mod(mul(std::move(value), 1u << residual_bits), 256);
                            if (j) value = add(std::move(value),
                                floor_(scratch::div(input[j - 1], 1u << (8 - residual_bits))));
                        }
                    }
                } else {
                    const unsigned j = i + whole_bytes;
                    value = right_byte(j);
                    if (residual_bits) value = add(
                        floor_(scratch::div(std::move(value), 1u << residual_bits)),
                        mul(mod(right_byte(j + 1), 1u << residual_bits), 1u << (8 - residual_bits)));
                }
                if (i + 1 == bytes && top_bits != 8) value = mod(std::move(value), top_modulus);
                generated.bytes.push_back(std::move(value));
            }
            return generated;
        });
}

Stmt SlotNumeric::compare(const std::string& predicate, unsigned bits,
        Expr destination, Expr lhs, Expr rhs) {
    unsigned bytes = bytes_for(bits);
    return operation("compare|" + predicate + "|" + std::to_string(bits),
        std::move(destination), {std::move(lhs), std::move(rhs)}, {bytes, bytes},
        [&](const std::vector<Bytes>& a) { return numeric_.compare(predicate, bits, a[0], a[1]); });
}

Stmt SlotNumeric::cast(const std::string& op, unsigned from_bits, unsigned to_bits,
        Expr destination, Expr source) {
    bytes_for(to_bits);
    return operation("cast|" + op + "|" + std::to_string(from_bits) + "|" + std::to_string(to_bits),
        std::move(destination), {std::move(source)}, {bytes_for(from_bits)},
        [&](const std::vector<Bytes>& a) { return numeric_.cast(op, from_bits, to_bits, a[0]); });
}

Stmt SlotNumeric::intrinsic(const std::string& name, unsigned bits, Expr destination,
        const std::vector<Expr>& source_slots, const std::vector<unsigned>& byte_lengths) {
    bytes_for(bits);
    return operation("intrinsic|" + name + "|" + std::to_string(bits),
        std::move(destination), source_slots, byte_lengths,
        [&](const std::vector<Bytes>& a) { return numeric_.intrinsic(name, bits, a); });
}

void SlotNumeric::install(Project& project) const {
    numeric_.install(project);
}

}
