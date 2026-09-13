#include "scratch/constants.hpp"
#include <algorithm>
#include <cstdint>
#include <set>

namespace scratch {
namespace {
using Octets = std::vector<unsigned>;
constexpr unsigned maximum_bytes = 200000;

unsigned storage_size(const Json& type) {
    const auto size = type.value("size", uint64_t(0));
    if (size > maximum_bytes) throw Error("constant storage exceeds the Scratch memory capacity");
    return static_cast<unsigned>(size);
}

unsigned bit_width(const Json& type) {
    const auto size = storage_size(type);
    const auto bits = type.value("bits", uint64_t(size) * 8);
    if (bits > uint64_t(size) * 8) throw Error("constant bit width exceeds its storage");
    return static_cast<unsigned>(bits);
}

unsigned byte_count(unsigned bits) {
    if (!bits || bits > maximum_bytes * 8) throw Error("invalid constant integer bit width");
    return (bits + 7) / 8;
}

Octets normalize(Octets bytes, unsigned bits) {
    bytes.resize(byte_count(bits), 0);
    if (bits % 8) bytes.back() &= (1u << (bits % 8)) - 1;
    return bytes;
}

Octets typed(Octets bytes, const Json& type) {
    bytes.resize(storage_size(type), 0);
    for (unsigned byte : bytes) if (byte > 255) throw Error("non-byte in constant representation");
    const auto kind = type.value("kind", "");
    if ((kind == "int" || kind == "pointer" || kind == "vector") && !bytes.empty()) {
        const auto bits = bit_width(type);
        if (!bits) throw Error("zero-width scalar/vector constant");
        if (bits % 8) bytes.back() &= (1u << (bits % 8)) - 1;
    }
    return bytes;
}

bool bit(const Octets& bytes, unsigned index) {
    return (bytes.at(index / 8) >> (index % 8)) & 1;
}

void assign_bit(Octets& bytes, unsigned index, bool value) {
    auto& byte = bytes.at(index / 8);
    const auto mask = 1u << (index % 8);
    byte = value ? byte | mask : byte & ~mask;
}

Octets extract_bits(const Octets& bytes, uint64_t offset, unsigned bits) {
    if (offset + bits > uint64_t(bytes.size()) * 8)
        throw Error("constant vector element exceeds storage");
    Octets result(byte_count(bits), 0);
    if (!(offset % 8) && !(bits % 8)) {
        std::copy_n(bytes.begin() + static_cast<size_t>(offset / 8), result.size(), result.begin());
    } else {
        for (unsigned i = 0; i < bits; ++i)
            if (bit(bytes, static_cast<unsigned>(offset) + i)) result[i / 8] |= 1u << (i % 8);
    }
    return result;
}

void insert_bits(Octets& destination, const Octets& source, uint64_t offset, unsigned bits) {
    if (offset + bits > uint64_t(destination.size()) * 8 || bits > uint64_t(source.size()) * 8)
        throw Error("constant vector element exceeds storage");
    if (!(offset % 8) && !(bits % 8)) {
        std::copy_n(source.begin(), bits / 8, destination.begin() + static_cast<size_t>(offset / 8));
    } else {
        for (unsigned i = 0; i < bits; ++i)
            assign_bit(destination, static_cast<unsigned>(offset) + i, bit(source, i));
    }
}

Octets literal(uint64_t value, unsigned bits) {
    Octets bytes(byte_count(bits), 0);
    for (unsigned i = 0; i < std::min<unsigned>(8, static_cast<unsigned>(bytes.size())); ++i) {
        bytes[i] = static_cast<unsigned>(value & 255);
        value >>= 8;
    }
    return normalize(std::move(bytes), bits);
}

Octets cast(const std::string& op, unsigned from, unsigned to, Octets bytes) {
    bytes = normalize(std::move(bytes), from);
    const bool negative = op == "sext" && to > from && bit(bytes, from - 1);
    if (negative && from % 8) bytes.back() |= 255u ^ ((1u << (from % 8)) - 1);
    bytes.resize(byte_count(to), negative ? 255 : 0);
    return normalize(std::move(bytes), to);
}

Octets add_sub(unsigned bits, const Octets& a, const Octets& b, bool subtract) {
    Octets result(a.size());
    unsigned carry = subtract ? 1 : 0;
    for (size_t i = 0; i < a.size(); ++i) {
        const unsigned value = a[i] + (subtract ? 255 - b[i] : b[i]) + carry;
        result[i] = value & 255;
        carry = value >> 8;
    }
    return normalize(std::move(result), bits);
}

Octets negate(unsigned bits, const Octets& bytes) {
    return add_sub(bits, Octets(bytes.size(), 0), bytes, true);
}

int compare_unsigned(const Octets& a, const Octets& b) {
    if (a.size() != b.size()) throw Error("constant integer storage mismatch");
    for (size_t i = a.size(); i-- > 0;)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

bool compare(const std::string& predicate, unsigned bits, Octets a, Octets b) {
    a = normalize(std::move(a), bits);
    b = normalize(std::move(b), bits);
    int relation = compare_unsigned(a, b);
    if (!predicate.empty() && predicate[0] == 's') {
        const bool sa = bit(a, bits - 1), sb = bit(b, bits - 1);
        if (sa != sb) relation = sa ? -1 : 1;
    }
    if (predicate == "eq") return relation == 0;
    if (predicate == "ne") return relation != 0;
    if (predicate == "ult" || predicate == "slt") return relation < 0;
    if (predicate == "ule" || predicate == "sle") return relation <= 0;
    if (predicate == "ugt" || predicate == "sgt") return relation > 0;
    if (predicate == "uge" || predicate == "sge") return relation >= 0;
    throw Error("unsupported constant integer predicate '" + predicate + "'");
}

// Return limit for any value >= limit without overflowing a host integer.
unsigned bounded_index(const Octets& bytes, unsigned limit) {
    unsigned value = 0;
    for (size_t i = bytes.size(); i-- > 0;) {
        if (value >= limit || uint64_t(value) * 256 + bytes[i] >= limit) return limit;
        value = value * 256 + bytes[i];
    }
    return value;
}

std::pair<Octets, Octets> divide(unsigned bits, const Octets& a, const Octets& divisor) {
    Octets quotient(a.size(), 0), remainder(a.size(), 0);
    if (std::all_of(divisor.begin(), divisor.end(), [](unsigned b) { return b == 0; }))
        return {quotient, remainder};
    if (compare_unsigned(a, divisor) < 0) return {quotient, a};
    if (divisor[0] == 1 && std::all_of(divisor.begin() + 1, divisor.end(), [](unsigned b) { return b == 0; }))
        return {a, remainder};
    for (unsigned i = bits; i-- > 0;) {
        unsigned carry = bit(a, i);
        for (auto& byte : remainder) {
            const unsigned value = byte * 2 + carry;
            byte = value & 255;
            carry = value >> 8;
        }
        // Before shifting remainder < divisor, so a single subtraction suffices.
        if (carry || compare_unsigned(remainder, divisor) >= 0) {
            remainder = add_sub(static_cast<unsigned>(a.size()) * 8, remainder, divisor, true);
            assign_bit(quotient, i, true);
        }
    }
    return {normalize(std::move(quotient), bits), normalize(std::move(remainder), bits)};
}

Octets binary(const std::string& op, unsigned bits, Octets a, Octets b) {
    a = normalize(std::move(a), bits);
    b = normalize(std::move(b), bits);
    if (op == "add" || op == "sub") return add_sub(bits, a, b, op == "sub");
    if (op == "and" || op == "or" || op == "xor") {
        for (size_t i = 0; i < a.size(); ++i)
            a[i] = op == "and" ? a[i] & b[i] : op == "or" ? a[i] | b[i] : a[i] ^ b[i];
        return a;
    }
    if (op == "mul") {
        Octets result(a.size(), 0);
        size_t used = b.size();
        while (used && !b[used - 1]) --used;
        for (size_t i = 0; i < a.size(); ++i) {
            if (!a[i]) continue;
            unsigned carry = 0;
            size_t j = 0;
            for (; j < used && i + j < result.size(); ++j) {
                const unsigned value = result[i + j] + a[i] * b[j] + carry;
                result[i + j] = value & 255;
                carry = value >> 8;
            }
            while (carry && i + j < result.size()) {
                const unsigned value = result[i + j] + carry;
                result[i + j++] = value & 255;
                carry = value >> 8;
            }
        }
        return normalize(std::move(result), bits);
    }
    if (op == "shl" || op == "lshr" || op == "ashr") {
        Octets result(a.size(), 0);
        const unsigned amount = bounded_index(b, bits);
        if (amount >= bits) return result;
        const bool sign = op == "ashr" && bit(a, bits - 1);
        for (unsigned i = 0; i < bits; ++i) {
            const bool value = op == "shl" ? i >= amount && bit(a, i - amount)
                : i + amount < bits ? bit(a, i + amount) : sign;
            if (value) result[i / 8] |= 1u << (i % 8);
        }
        return result;
    }
    if (op == "udiv" || op == "urem" || op == "sdiv" || op == "srem") {
        const bool signed_op = op[0] == 's';
        const bool sa = signed_op && bit(a, bits - 1), sb = signed_op && bit(b, bits - 1);
        if (sa) a = negate(bits, a);
        if (sb) b = negate(bits, b);
        auto pair = divide(bits, a, b);
        const bool quotient = op == "udiv" || op == "sdiv";
        auto result = quotient ? std::move(pair.first) : std::move(pair.second);
        return (quotient ? sa != sb : sa) ? negate(bits, result) : result;
    }
    throw Error("unsupported constant integer operation '" + op + "'");
}

unsigned aggregate_offset(Json type, const Json& indices) {
    uint64_t offset = 0;
    for (const auto& index : indices) {
        const auto i = index.get<uint64_t>();
        if (type.value("kind", "") == "struct") {
            if (i >= type.at("fields").size()) throw Error("constant structure index out of range");
            const auto field_offset = type.at("offsets").at(static_cast<size_t>(i)).get<uint64_t>();
            if (field_offset > maximum_bytes) throw Error("constant aggregate offset exceeds storage");
            offset += field_offset;
            type = Json(type.at("fields").at(static_cast<size_t>(i)));
        } else if (type.value("kind", "") == "array") {
            if (i >= type.at("count").get<uint64_t>()) throw Error("constant array index out of range");
            const Json element = type.at("element");
            const auto stride = element.value("alloc_size", uint64_t(storage_size(element)));
            if (stride && i > maximum_bytes / stride) throw Error("constant aggregate offset exceeds storage");
            offset += i * stride;
            type = element;
        } else throw Error("invalid constant aggregate index");
        if (offset > maximum_bytes) throw Error("constant aggregate offset exceeds storage");
    }
    return static_cast<unsigned>(offset);
}

class Evaluator {
    const std::function<Octets(const Json&)>& resolve;

    Octets gep(const Json& instruction) {
        const auto& type = instruction.at("type");
        const auto& base_operand = instruction.at("operands").at(0);
        auto base = value(base_operand);
        if (type.value("kind", "") == "vector") {
            const auto& element = type.at("element");
            const auto width = bit_width(element);
            Octets result(storage_size(type), 0);
            const auto count = type.at("count").get<unsigned>();
            for (unsigned lane = 0; lane < count; ++lane) {
                Json scalar = instruction;
                scalar["type"] = element;
                if (base_operand.at("type").value("kind", "") == "vector")
                    scalar["operands"][0] = {{"kind", "bytes"}, {"type", element},
                        {"bytes", extract_bits(base, uint64_t(lane) * width, width)}};
                for (auto& step : scalar["gep"]) if (step.contains("index")) {
                    const Json index = step.at("index");
                    if (index.at("type").value("kind", "") == "vector") {
                        const auto& index_type = index.at("type").at("element");
                        const auto index_width = bit_width(index_type);
                        step["index"] = {{"kind", "bytes"}, {"type", index_type},
                            {"bytes", extract_bits(value(index), uint64_t(lane) * index_width, index_width)}};
                    }
                }
                insert_bits(result, gep(scalar), uint64_t(lane) * width, width);
            }
            return result;
        }
        const unsigned pbits = bit_width(type);
        unsigned ibits = type.value("index_bits", instruction.value("index_bits", pbits));
        if (!ibits) ibits = pbits;
        if (ibits > pbits) throw Error("GEP index width exceeds pointer width");
        auto sum = cast("trunc", pbits, ibits, base);
        for (const auto& step : instruction.at("gep")) {
            Octets delta;
            if (step.contains("offset")) delta = literal(step.at("offset").get<uint64_t>(), ibits);
            else {
                const auto& index = step.at("index");
                const auto source_width = bit_width(index.at("type"));
                auto x = cast(source_width > ibits ? "trunc" : "sext", source_width, ibits, value(index));
                delta = binary("mul", ibits, std::move(x), literal(step.at("stride").get<uint64_t>(), ibits));
            }
            sum = binary("add", ibits, std::move(sum), std::move(delta));
        }
        // LLVM GEP arithmetic wraps in the index width and leaves higher
        // pointer bits unchanged. One-past/non-dereferenceable results are legal.
        insert_bits(base, sum, 0, ibits);
        return base;
    }

    Octets operation(const Json& instruction) {
        const auto op = instruction.at("op").get<std::string>();
        const auto& type = instruction.at("type");
        const auto& operands = instruction.at("operands");
        if (op == "gep" || op == "getelementptr") return gep(instruction);
        if (op == "freeze") return value(operands.at(0));
        if (op == "bitcast") {
            auto result = value(operands.at(0));
            if (result.size() != storage_size(type)) throw Error("invalid constant bitcast storage size");
            return result;
        }
        if (op == "select") {
            const auto condition = value(operands.at(0));
            if (operands.at(0).at("type").value("kind", "") != "vector")
                return value(operands.at(condition.at(0) ? 1 : 2));
            const auto a = value(operands.at(1)), b = value(operands.at(2));
            const auto width = bit_width(type.at("element"));
            Octets result(storage_size(type), 0);
            for (unsigned lane = 0; lane < type.at("count").get<unsigned>(); ++lane)
                insert_bits(result, extract_bits(bit(condition, lane) ? a : b, uint64_t(lane) * width, width),
                    uint64_t(lane) * width, width);
            return result;
        }
        if (op == "extractvalue" || op == "insertvalue") {
            auto aggregate = value(operands.at(0));
            const auto offset = aggregate_offset(operands.at(0).at("type"), instruction.at("indices"));
            if (op == "extractvalue") {
                const auto count = storage_size(type);
                if (uint64_t(offset) + count > aggregate.size()) throw Error("constant extraction exceeds storage");
                return Octets(aggregate.begin() + offset, aggregate.begin() + offset + count);
            }
            const auto inserted = value(operands.at(1));
            if (uint64_t(offset) + inserted.size() > aggregate.size()) throw Error("constant insertion exceeds storage");
            std::copy(inserted.begin(), inserted.end(), aggregate.begin() + offset);
            return aggregate;
        }
        if (op == "extractelement" || op == "insertelement") {
            auto vector = value(operands.at(0));
            const auto& vector_type = operands.at(0).at("type");
            const auto count = vector_type.at("count").get<unsigned>();
            const auto width = bit_width(vector_type.at("element"));
            const auto index = bounded_index(value(operands.at(op == "extractelement" ? 1 : 2)), count);
            if (index >= count) return Octets(storage_size(type), 0);
            if (op == "extractelement") return extract_bits(vector, uint64_t(index) * width, width);
            insert_bits(vector, value(operands.at(1)), uint64_t(index) * width, width);
            return vector;
        }
        if (op == "shufflevector") {
            const auto a = value(operands.at(0)), b = value(operands.at(1));
            const auto& vector_type = operands.at(0).at("type");
            const auto count = vector_type.at("count").get<unsigned>();
            const auto width = bit_width(vector_type.at("element"));
            Octets result(storage_size(type), 0);
            uint64_t lane = 0;
            for (const auto& mask : instruction.at("mask")) {
                const auto index = mask.get<int64_t>();
                if (index >= 0) {
                    if (uint64_t(index) >= uint64_t(count) * 2) throw Error("constant shuffle index out of range");
                    insert_bits(result, extract_bits(uint64_t(index) < count ? a : b,
                        uint64_t(index % count) * width, width), lane * width, width);
                }
                ++lane;
            }
            return result;
        }
        static const std::set<std::string> casts = {"trunc", "zext", "sext", "ptrtoint", "inttoptr"};
        static const std::set<std::string> binaries = {
            "add", "sub", "mul", "udiv", "sdiv", "urem", "srem", "and", "or", "xor", "shl", "lshr", "ashr"};
        if (casts.count(op) || binaries.count(op) || op == "icmp") {
            const auto& source = operands.at(0).at("type");
            auto a = value(operands.at(0));
            auto b = operands.size() > 1 ? value(operands.at(1)) : Octets{};
            const bool vector = source.value("kind", "") == "vector";
            const auto width = bit_width(vector ? source.at("element") : source);
            const auto destination_width = op == "icmp" ? 1 : bit_width(vector ? type.at("element") : type);
            auto scalar = [&](Octets x, Octets y) -> Octets {
                if (casts.count(op)) return cast(op, width, destination_width, std::move(x));
                if (op == "icmp") return {compare(instruction.at("predicate"), width, std::move(x), std::move(y)) ? 1u : 0u};
                return binary(op, width, std::move(x), std::move(y));
            };
            if (!vector) return scalar(std::move(a), std::move(b));
            Octets result(storage_size(type), 0);
            for (unsigned lane = 0; lane < source.at("count").get<unsigned>(); ++lane)
                insert_bits(result, scalar(extract_bits(a, uint64_t(lane) * width, width),
                    b.empty() ? Octets{} : extract_bits(b, uint64_t(lane) * width, width)),
                    uint64_t(lane) * destination_width, destination_width);
            return result;
        }
        throw Error("unsupported constant expression '" + op + "'");
    }

public:
    explicit Evaluator(const std::function<Octets(const Json&)>& resolver) : resolve(resolver) {}

    Octets value(const Json& constant) {
        const auto& type = constant.at("type");
        const auto size = storage_size(type);
        const auto kind = constant.value("kind", "");
        if (kind == "zero" || kind == "undef" || kind == "poison") return Octets(size, 0);
        if (kind == "bytes") return typed(constant.at("bytes").get<Octets>(), type);
        if (kind == "symbol" || kind == "blockaddress") return typed(resolve(constant), type);
        if (kind == "constexpr") return typed(operation(constant), type);
        if (kind == "aggregate") {
            Octets result(size, 0);
            const auto aggregate_kind = type.value("kind", "");
            uint64_t index = 0;
            for (const auto& element : constant.at("elements")) {
                const auto bytes = value(element);
                if (aggregate_kind == "vector") {
                    const auto bits = bit_width(type.at("element"));
                    insert_bits(result, bytes, index * bits, bits);
                } else {
                    uint64_t offset;
                    if (aggregate_kind == "struct") offset = type.at("offsets").at(static_cast<size_t>(index)).get<uint64_t>();
                    else if (aggregate_kind == "array") {
                        const auto stride = type.at("element").value("alloc_size", uint64_t(storage_size(type.at("element"))));
                        if (stride && index > maximum_bytes / stride) throw Error("constant aggregate offset exceeds storage");
                        offset = index * stride;
                    } else throw Error("invalid constant aggregate type");
                    if (offset > result.size() || bytes.size() > result.size() - offset)
                        throw Error("constant aggregate initializer exceeds storage");
                    std::copy(bytes.begin(), bytes.end(), result.begin() + static_cast<size_t>(offset));
                }
                ++index;
            }
            return typed(std::move(result), type);
        }
        throw Error("unsupported constant operand kind '" + kind + "'");
    }
};
}

std::vector<unsigned> evaluate_constant(
    const Json& constant,
    const std::function<std::vector<unsigned>(const Json&)>& resolve_symbol) {
    return Evaluator(resolve_symbol).value(constant);
}
}
