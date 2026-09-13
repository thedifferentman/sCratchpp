#include "scratch/numeric.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace scratch {
namespace {
unsigned byte_count(unsigned bits) {
    if (!bits) throw Error("zero-width integer is not supported");
    return (bits - 1) / 8 + 1;
}
unsigned bounded_byte_count(const Bytes& bytes) {
    // Internal algorithms may append a guard byte and convert its count to bits.
    // Check this explicitly before narrowing size_t on 64-bit hosts.
    constexpr auto limit = std::numeric_limits<unsigned>::max() / 8 - 1;
    if (bytes.size() > limit) throw Error("integer byte representation exceeds backend index capacity");
    return static_cast<unsigned>(bytes.size());
}
unsigned top_bits(unsigned bits) { return (bits - 1) % 8 + 1; }
unsigned top_modulus(unsigned bits) { return 1u << top_bits(bits); }

// Every primitive reads snapshots and leaves materialized bytes. This is important
// for Scratch reporters: embedding a load expression must not reload its input
// after another byte of an overlapping destination has been updated.
class Emitter {
public:
    Script code;
    std::function<std::string(const std::string&)> fresh;
    std::set<std::string>& tables;
    Emitter(std::function<std::string(const std::string&)> f,
            std::set<std::string>& t) : fresh(std::move(f)), tables(t) {}
    Expr save(Expr v, const std::string& hint = "v") {
        auto name = fresh(hint);
        code.push_back(set(name, std::move(v)));
        return var(name);
    }
    std::string name(const Expr& v) const {
        return v.at("fields").at("VARIABLE").at(0).get<std::string>();
    }
    void assign(const Expr& v, Expr value) { code.push_back(set(name(v), std::move(value))); }
    Bytes zero(unsigned bits, const std::string& hint = "r") {
        Bytes out;
        for (unsigned i = 0; i < byte_count(bits); ++i) out.push_back(save(0, hint));
        return out;
    }
    Bytes snapshot(unsigned bits, const Bytes& input, const std::string& hint = "a") {
        unsigned n = byte_count(bits);
        if (input.size() < n) throw Error("integer operand is shorter than its type");
        Bytes result;
        for (unsigned i = 0; i < n; ++i)
            result.push_back(save(i + 1 == n ? mod(input[i], top_modulus(bits)) : input[i], hint));
        return result;
    }
    void normalize(Bytes& a, unsigned bits) {
        if (top_bits(bits) != 8) assign(a.back(), mod(a.back(), top_modulus(bits)));
    }
    Expr sign(const Bytes& a, unsigned bits) {
        return floor_(scratch::div(a.back(), 1u << (top_bits(bits) - 1)));
    }
    Expr is_zero(const Bytes& a) {
        Expr result = eq(a[0], 0);
        for (unsigned i = 1; i < a.size(); ++i) result = land(std::move(result), eq(a[i], 0));
        return result;
    }
    Expr equal(const Bytes& a, const Bytes& b) {
        Expr result = eq(a[0], b[0]);
        for (unsigned i = 1; i < a.size(); ++i) result = land(std::move(result), eq(a[i], b[i]));
        return result;
    }
    Expr less(const Bytes& a, const Bytes& b) {
        Expr result = lt(a[0], b[0]);
        for (unsigned i = 1; i < a.size(); ++i)
            result = lor(lt(a[i], b[i]), land(eq(a[i], b[i]), std::move(result)));
        return result;
    }
    void conditional(Expr condition, const std::function<void()>& yes,
                     const std::function<void()>& no = {}) {
        Script outer = std::move(code); code.clear();
        yes(); Script y = std::move(code); code.clear();
        if (no) no();
        Script n = std::move(code); code = std::move(outer);
        code.push_back(iff(std::move(condition), std::move(y), std::move(n)));
    }
    void loop(Expr count, const std::function<void()>& body) {
        Script outer = std::move(code); code.clear();
        body(); Script b = std::move(code); code = std::move(outer);
        code.push_back(repeat(std::move(count), std::move(b)));
    }
    Expr boolean(Expr condition) {
        Expr result = save(0, "bool");
        conditional(std::move(condition), [&] { assign(result, 1); });
        return result;
    }
    Bytes addsub(unsigned bits, const Bytes& a, const Bytes& b, bool subtract) {
        Bytes out; Expr carry = save(0, "carry");
        for (unsigned i = 0; i < a.size(); ++i) {
            Expr v = save(add(subtract ? sub(a[i], b[i]) : add(a[i], b[i]), carry), "sum");
            out.push_back(save(mod(v, 256), "r"));
            assign(carry, floor_(scratch::div(v, 256)));
        }
        normalize(out, bits);
        return out;
    }
    void negate(Bytes& a, unsigned bits) {
        Expr carry = save(1, "carry");
        for (auto& b : a) {
            Expr v = save(add(sub(255, b), carry), "neg");
            assign(b, mod(v, 256)); assign(carry, floor_(scratch::div(v, 256)));
        }
        normalize(a, bits);
    }
    Bytes product(unsigned bits, const Bytes& a, const Bytes& b) {
        Bytes out = zero(bits);
        for (unsigned i = 0; i < a.size(); ++i) {
            Expr carry = save(0, "carry");
            for (unsigned j = 0; i + j < out.size(); ++j) {
                // <= 255*255 + 255 + 255 = 65535, exactly representable.
                Expr v = save(add(add(mul(a[i], b[j]), out[i+j]), carry), "product");
                assign(out[i+j], mod(v, 256)); assign(carry, floor_(scratch::div(v, 256)));
            }
        }
        normalize(out, bits); return out;
    }
    void shift_one(Bytes& a, unsigned bits, bool left, Expr sign_fill = 0) {
        Expr carry = save(0, "carry");
        if (left) {
            for (auto& b : a) {
                Expr v = save(add(mul(b, 2), carry), "shift");
                assign(b, mod(v, 256)); assign(carry, floor_(scratch::div(v, 256)));
            }
            normalize(a, bits);
        } else {
            for (unsigned i = bounded_byte_count(a); i-- > 0;) {
                Expr v = save(add(a[i], mul(carry, 256)), "shift");
                assign(a[i], floor_(scratch::div(v, 2))); assign(carry, mod(v, 2));
            }
            assign(a.back(), add(a.back(), mul(sign_fill, 1u << (top_bits(bits)-1))));
        }
    }
    Bytes shift(unsigned bits, Bytes a, const Bytes& b, const std::string& op) {
        Expr amount = save(0, "amount");
        // Saturation bounds all intermediate values even for enormous shift operands.
        for (unsigned i = bounded_byte_count(b); i-- > 0;)
            conditional(lt(amount, bits), [&] { assign(amount, add(mul(amount, 256), b[i])); });
        Expr fill = op == "ashr" ? save(sign(a, bits), "sign") : Expr(0);
        conditional(lt(amount, bits), [&] {
            loop(amount, [&] { shift_one(a, bits, op == "shl", fill); });
        }, [&] { for (const auto& v : a) assign(v, 0); });
        return a;
    }
    std::pair<Bytes, Bytes> quotient(unsigned bits, Bytes a, const Bytes& divisor) {
        unsigned n = bounded_byte_count(a);
        Bytes rem = zero((n + 1) * 8, "rem");
        Bytes d = divisor; d.push_back(0);
        conditional(lnot(is_zero(divisor)), [&] {
            loop(bits, [&] {
                Expr bit = save(sign(a, bits), "bit");
                shift_one(a, bits, true);
                shift_one(rem, (n + 1) * 8, true);
                assign(rem[0], add(rem[0], bit));
                conditional(lnot(less(rem, d)), [&] {
                    Bytes difference = addsub((n + 1) * 8, rem, d, true);
                    for (unsigned i = 0; i < rem.size(); ++i) assign(rem[i], difference[i]);
                    assign(a[0], add(a[0], 1));
                });
            });
        }, [&] { for (const auto& v : a) assign(v, 0); });
        rem.resize(n); normalize(rem, bits);
        return {a, rem};
    }
    Bytes compute(const std::string& op, unsigned bits, Bytes a, Bytes b) {
        if (op == "add" || op == "sub") return addsub(bits, a, b, op == "sub");
        if (op == "mul") return product(bits, a, b);
        if (op == "and" || op == "or" || op == "xor") {
            std::string table = "__numeric_" + op;
            tables.insert(table); Bytes out;
            for (unsigned i = 0; i < a.size(); ++i)
                out.push_back(save(item(table, add(add(mul(a[i], 256), b[i]), 1)), "r"));
            normalize(out, bits); return out;
        }
        if (op == "shl" || op == "lshr" || op == "ashr") return shift(bits, a, b, op);
        if (op == "udiv" || op == "urem") {
            auto result = quotient(bits, a, b); return op == "udiv" ? result.first : result.second;
        }
        if (op == "sdiv" || op == "srem") {
            Expr sa = save(sign(a, bits), "sign"), sb = save(sign(b, bits), "sign");
            conditional(eq(sa, 1), [&] { negate(a, bits); });
            conditional(eq(sb, 1), [&] { negate(b, bits); });
            auto result = quotient(bits, a, b);
            Bytes out = op == "sdiv" ? result.first : result.second;
            conditional(op == "sdiv" ? lnot(eq(sa, sb)) : eq(sa, 1), [&] { negate(out, bits); });
            return out;
        }
        throw Error("unsupported integer operation: " + op);
    }
    Bytes resize(unsigned from_bits, unsigned to_bits, Bytes in, bool signed_extend) {
        const auto n = byte_count(to_bits);
        Expr fill = signed_extend && to_bits > from_bits
            ? save(mul(sign(in, from_bits), 255), "fill") : Expr(0);
        Bytes out;
        for (unsigned i = 0; i < n; ++i) out.push_back(save(i < in.size() ? in[i] : fill, "cast"));
        if (signed_extend && to_bits > from_bits && top_bits(from_bits) != 8)
            assign(out[in.size()-1], add(out[in.size()-1], mul(scratch::div(fill, 255), 256-top_modulus(from_bits))));
        normalize(out, to_bits); return out;
    }
    Bytes small_result(unsigned bits, Expr number) {
        Bytes out; Expr v = save(std::move(number), "count");
        for (unsigned i = 0; i < byte_count(bits); ++i) {
            out.push_back(save(mod(v, 256), "r"));
            assign(v, floor_(scratch::div(v, 256)));
        }
        normalize(out, bits); return out;
    }
};
}

std::string Numeric::temporary(const std::string& hint) {
    return temporary_prefix_ + hint + "_" + std::to_string(counter_++);
}

NumericResult Numeric::shared(const std::string& signature, const std::vector<Bytes>& args,
                              const std::function<NumericResult(const std::vector<Bytes>&)>& generate) {
    std::string key = signature;
    std::vector<Expr> actual;
    for (const auto& bytes : args) {
        key += ":" + std::to_string(bounded_byte_count(bytes));
        actual.insert(actual.end(), bytes.begin(), bytes.end());
    }
    auto found = helpers_.find(key);
    if (found == helpers_.end()) {
        Helper helper;
        helper.name = temporary("helper");
        std::vector<Bytes> parameters;
        for (const auto& bytes : args) {
            Bytes operand;
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                std::string name = "a" + std::to_string(helper.parameters.size());
                helper.parameters.push_back(name);
                operand.push_back(arg(name));
            }
            parameters.push_back(std::move(operand));
        }
        NumericResult generated;
        // Reuse the fully tested inline emitter, never recursively introduce a
        // helper call while materializing one helper's implementation.
        building_helper_ = true;
        try { generated = generate(parameters); }
        catch (...) { building_helper_ = false; throw; }
        building_helper_ = false;
        helper.body = std::move(generated.code);
        for (const auto& byte : generated.bytes) {
            std::string output = temporary("helper_result");
            helper.outputs.push_back(output);
            helper.body.push_back(set(output, byte));
        }
        found = helpers_.emplace(std::move(key), std::move(helper)).first;
    }
    NumericResult result;
    result.code.push_back(call(found->second.name, actual));
    // Outputs belong to the reusable helper. Copy them immediately to a unique
    // call-site snapshot, so a later lane or chained operation cannot overwrite
    // an earlier result that has not yet been committed to virtual memory.
    for (const auto& output : found->second.outputs) {
        std::string snapshot = temporary("call_result");
        result.code.push_back(set(snapshot, var(output)));
        result.bytes.push_back(var(snapshot));
    }
    return result;
}

NumericResult Numeric::binary(const std::string& op, unsigned bits, const Bytes& lhs, const Bytes& rhs) {
    if (shared_helpers_ && !building_helper_)
        return shared("binary|" + op + "|" + std::to_string(bits), {lhs, rhs},
            [&](const std::vector<Bytes>& a) { return binary(op, bits, a[0], a[1]); });
    Emitter e([this](const std::string& s) { return temporary(s); }, tables_);
    auto a = e.snapshot(bits, lhs, "lhs"), b = e.snapshot(bits, rhs, "rhs");
    auto result = e.compute(op, bits, std::move(a), std::move(b));
    return {std::move(e.code), std::move(result)};
}

NumericResult Numeric::divrem(unsigned bits, const Bytes& lhs, const Bytes& rhs, bool signed_op) {
    if (shared_helpers_ && !building_helper_)
        return shared(std::string("divrem|") + (signed_op ? "s|" : "u|") + std::to_string(bits), {lhs, rhs},
            [&](const std::vector<Bytes>& a) { return divrem(bits, a[0], a[1], signed_op); });
    Emitter e([this](const std::string& s) { return temporary(s); }, tables_);
    Bytes a = e.snapshot(bits, lhs, "lhs"), b = e.snapshot(bits, rhs, "rhs");
    Expr sa = 0, sb = 0;
    if (signed_op) {
        sa = e.save(e.sign(a, bits), "sign");
        sb = e.save(e.sign(b, bits), "sign");
        e.conditional(eq(sa, 1), [&] { e.negate(a, bits); });
        e.conditional(eq(sb, 1), [&] { e.negate(b, bits); });
    }
    auto results = e.quotient(bits, a, b);
    if (signed_op) {
        e.conditional(lnot(eq(sa, sb)), [&] { e.negate(results.first, bits); });
        e.conditional(eq(sa, 1), [&] { e.negate(results.second, bits); });
    }
    Bytes result = std::move(results.first);
    result.insert(result.end(), results.second.begin(), results.second.end());
    return {std::move(e.code), std::move(result)};
}

NumericResult Numeric::compare(const std::string& predicate, unsigned bits, const Bytes& lhs, const Bytes& rhs) {
    if (shared_helpers_ && !building_helper_)
        return shared("compare|" + predicate + "|" + std::to_string(bits), {lhs, rhs},
            [&](const std::vector<Bytes>& a) { return compare(predicate, bits, a[0], a[1]); });
    Emitter e([this](const std::string& s) { return temporary(s); }, tables_);
    auto a = e.snapshot(bits, lhs, "lhs"), b = e.snapshot(bits, rhs, "rhs");
    Expr condition;
    if (predicate == "eq" || predicate == "ne") {
        condition = e.equal(a, b); if (predicate == "ne") condition = lnot(condition);
    } else {
        std::string p = predicate;
        bool signed_compare = !p.empty() && p[0] == 's';
        if (p != "ult" && p != "ule" && p != "ugt" && p != "uge" &&
            p != "slt" && p != "sle" && p != "sgt" && p != "sge")
            throw Error("unsupported integer comparison: " + predicate);
        if (p[1] == 'g') std::swap(a, b);
        condition = e.less(a, b);
        if (signed_compare) {
            Expr sa = e.save(e.sign(a, bits), "sign"), sb = e.save(e.sign(b, bits), "sign");
            condition = lor(gt(sa, sb), land(eq(sa, sb), condition));
        }
        if (p[2] == 'e') condition = lor(std::move(condition), e.equal(a, b));
    }
    Expr value = e.boolean(std::move(condition));
    return {std::move(e.code), Bytes{value}};
}

NumericResult Numeric::cast(const std::string& op, unsigned from_bits, unsigned to_bits, const Bytes& value) {
    if (shared_helpers_ && !building_helper_)
        return shared("cast|" + op + "|" + std::to_string(from_bits) + "|" + std::to_string(to_bits), {value},
            [&](const std::vector<Bytes>& a) { return cast(op, from_bits, to_bits, a[0]); });
    if (op != "trunc" && op != "zext" && op != "sext" && op != "bitcast" &&
        op != "ptrtoint" && op != "inttoptr") throw Error("unsupported integer cast: " + op);
    if (op == "bitcast" && from_bits != to_bits) throw Error("bitcast changes bit width");
    if ((op == "trunc" && to_bits > from_bits) ||
        ((op == "zext" || op == "sext") && to_bits < from_bits))
        throw Error("integer cast has inconsistent source and destination widths");
    Emitter e([this](const std::string& s) { return temporary(s); }, tables_);
    auto result = e.resize(from_bits, to_bits, e.snapshot(from_bits, value), op == "sext");
    return {std::move(e.code), std::move(result)};
}

NumericResult Numeric::intrinsic(const std::string& name, unsigned bits, const std::vector<Bytes>& args) {
    if (shared_helpers_ && !building_helper_)
        return shared("intrinsic|" + name + "|" + std::to_string(bits), args,
            [&](const std::vector<Bytes>& a) { return intrinsic(name, bits, a); });
    Emitter e([this](const std::string& s) { return temporary(s); }, tables_);
    if (args.empty()) throw Error("integer intrinsic has no operand: " + name);
    std::string op = name;
    if (op.rfind("llvm.", 0) == 0) op.erase(0, 5);
    auto matches = [&](const std::string& s) { return op == s || op.rfind(s + ".", 0) == 0; };
    Bytes a = e.snapshot(bits, args[0]);
    Bytes result;
    if (matches("ctpop")) {
        tables_.insert("__numeric_popcount"); Expr count = e.save(0, "count");
        for (auto& b : a) e.assign(count, add(count, item("__numeric_popcount", add(b, 1))));
        result = e.small_result(bits, count);
    } else if (matches("ctlz") || matches("cttz")) {
        bool leading = matches("ctlz");
        std::string table = leading ? "__numeric_clz" : "__numeric_ctz";
        tables_.insert(table);
        Expr count = e.save(0, "count"), found = e.save(0, "found");
        for (unsigned j = 0; j < a.size(); ++j) {
            unsigned i = leading ? bounded_byte_count(a)-1-j : j;
            unsigned width = i + 1 == a.size() ? top_bits(bits) : 8;
            e.conditional(eq(found, 0), [&] {
                e.conditional(eq(a[i], 0), [&] { e.assign(count, add(count, width)); }, [&] {
                    Expr c = item(table, add(a[i], 1));
                    if (leading && width != 8) c = sub(c, 8-width);
                    e.assign(count, add(count, c)); e.assign(found, 1);
                });
            });
        }
        result = e.small_result(bits, count);
    } else if (matches("bswap")) {
        if (bits % 16 != 0) throw Error("llvm.bswap requires a multiple-of-16 integer width");
        result = a; std::reverse(result.begin(), result.end());
    } else if (matches("bitreverse")) {
        tables_.insert("__numeric_reverse");
        for (unsigned i = bounded_byte_count(a); i-- > 0;) result.push_back(e.save(item("__numeric_reverse", add(a[i], 1))));
        if (top_bits(bits) != 8)
            e.loop(8-top_bits(bits), [&] { e.shift_one(result, bounded_byte_count(result)*8, false); });
        e.normalize(result, bits);
    } else if (matches("fshl") || matches("fshr")) {
        if (args.size() < 3) throw Error("funnel shift lacks operands: " + name);
        Bytes b = e.snapshot(bits, args[1]);
        Bytes shift = e.snapshot(bits, args[2]);
        Expr amount = e.save(0, "amount");
        for (unsigned i = bounded_byte_count(shift); i-- > 0;)
            e.assign(amount, mod(add(mul(amount, 256), shift[i]), bits));
        bool left = matches("fshl");
        e.loop(amount, [&] {
            Expr bit = e.save(left ? e.sign(b, bits) : mod(a[0], 2), "bit");
            e.shift_one(a, bits, left);
            e.shift_one(b, bits, left);
            if (left) e.assign(a[0], add(a[0], bit));
            else e.assign(b.back(), add(b.back(), mul(bit, top_modulus(bits)/2)));
        });
        result = left ? a : b;
    } else if (matches("abs")) {
        e.conditional(eq(e.sign(a, bits), 1), [&] { e.negate(a, bits); }); result = a;
    } else if (matches("smax") || matches("smin") || matches("umax") || matches("umin")) {
        if (args.size() < 2) throw Error("binary integer intrinsic lacks second operand: " + name);
        Bytes b = e.snapshot(bits, args[1]);
        Expr choose_a = e.less(a, b);
        if (op[0] == 's') {
            Expr sa = e.save(e.sign(a, bits)), sb = e.save(e.sign(b, bits));
            choose_a = lor(gt(sa, sb), land(eq(sa, sb), choose_a));
        }
        if (op.substr(1, 3) == "max") choose_a = lnot(choose_a);
        e.conditional(lnot(choose_a), [&] { for (unsigned i=0; i<a.size(); ++i) e.assign(a[i],b[i]); });
        result = a;
    } else {
        std::string arithmetic;
        bool overflow = op.find(".with.overflow") != std::string::npos;
        bool saturating = op.find(".sat") != std::string::npos;
        if ((overflow || saturating) && op.size() >= 4 && (op[0]=='s' || op[0]=='u'))
            arithmetic = op.substr(1, 3);
        if ((arithmetic != "add" && arithmetic != "sub" && arithmetic != "mul") ||
            (saturating && arithmetic == "mul"))
            throw Error("unsupported integer intrinsic: " + name);
        if (args.size() < 2) throw Error("binary integer intrinsic lacks second operand: " + name);
        Bytes b = e.snapshot(bits, args[1]);
        bool signed_op = op[0] == 's';
        unsigned wide_bits = arithmetic == "mul" ? bits * 2 : bits + 1;
        Bytes wide_a = e.resize(bits, wide_bits, a, signed_op);
        Bytes wide_b = e.resize(bits, wide_bits, b, signed_op);
        Bytes wide = e.compute(arithmetic, wide_bits, wide_a, wide_b);
        result = e.resize(wide_bits, bits, wide, false);
        Bytes back = e.resize(bits, wide_bits, result, signed_op);
        Expr overflowed = e.boolean(lnot(e.equal(wide, back)));
        if (overflow) result.push_back(overflowed);
        else e.conditional(eq(overflowed, 1), [&] {
            if (!signed_op) {
                for (unsigned i=0; i<result.size(); ++i)
                    e.assign(result[i], arithmetic == "sub" ? Expr(0) : Expr(i+1==result.size() ? top_modulus(bits)-1 : 255));
            } else {
                // On signed overflow the first operand's sign determines the bound.
                e.conditional(eq(e.sign(a,bits), 1), [&] {
                    for (unsigned i=0; i<result.size(); ++i) e.assign(result[i], i+1==result.size() ? top_modulus(bits)/2 : 0);
                }, [&] {
                    for (unsigned i=0; i<result.size(); ++i) e.assign(result[i], i+1==result.size() ? top_modulus(bits)/2-1 : 255);
                });
            }
        });
    }
    return {std::move(e.code), std::move(result)};
}

void Numeric::install(Project& project) const {
    for (const auto& entry : helpers_)
        project.procedure(entry.second.name, entry.second.parameters, entry.second.body);
    for (const auto& name : tables_) {
        Json values = Json::array();
        if (name == "__numeric_and" || name == "__numeric_or" || name == "__numeric_xor") {
            for (unsigned a = 0; a < 256; ++a) for (unsigned b = 0; b < 256; ++b)
                values.push_back(name == "__numeric_and" ? a & b : name == "__numeric_or" ? a | b : a ^ b);
        } else for (unsigned a = 0; a < 256; ++a) {
            unsigned result = 0;
            if (name == "__numeric_popcount") { for (unsigned b=a;b;b>>=1) result += b&1; }
            else if (name == "__numeric_reverse") { for (unsigned i=0;i<8;++i) result = (result<<1) | ((a>>i)&1); }
            else if (name == "__numeric_clz") { while (result<8 && !(a & (128u>>result))) ++result; }
            else if (name == "__numeric_ctz") { while (result<8 && !(a & (1u<<result))) ++result; }
            else throw Error("unknown numeric lookup table: " + name);
            values.push_back(result);
        }
        project.lists[name] = std::move(values);
    }
}
}

