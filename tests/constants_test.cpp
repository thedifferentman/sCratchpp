#include "scratch/constants.hpp"
#include <cstdint>
#include <iostream>
#include <random>

using namespace scratch;
namespace {
using Octets = std::vector<unsigned>;
unsigned checks = 0;

Json integer(unsigned bits) {
    return {{"kind", "int"}, {"bits", bits}, {"size", (bits + 7) / 8}, {"align", 1}};
}
Json constant(const Json& type, uint64_t n) {
    Octets bytes(type.at("size").get<unsigned>(), 0);
    for (unsigned i = 0; i < bytes.size() && i < 8; ++i) { bytes[i] = n & 255; n >>= 8; }
    return {{"kind", "bytes"}, {"type", type}, {"bytes", bytes}};
}
Json expression(const char* op, const Json& type, Json operands) {
    return {{"kind", "constexpr"}, {"op", op}, {"type", type}, {"operands", operands}};
}
Octets evaluate(const Json& value) {
    return evaluate_constant(value, [](const Json& symbol) -> Octets {
        if (symbol.value("kind", "") == "blockaddress") return {0x78, 0x56, 0x34, 0x12};
        if (symbol.value("name", "") == "global") return {255, 255, 255, 255, 0x78, 0x56, 0x34, 0x12};
        throw Error("unexpected symbol");
    });
}
void expect(const Json& value, const Octets& expected) {
    if (evaluate(value) != expected) throw Error("constant evaluation mismatch: " + value.dump());
    ++checks;
}
void expect_number(const Json& value, uint64_t expected) {
    expect(value, evaluate(constant(value.at("type"), expected)));
}
void rejected(const Json& value) {
    try { evaluate(value); } catch (const Error&) { ++checks; return; }
    throw Error("invalid constant accepted");
}
}

int main() {
    try {
        std::mt19937_64 random(0x73637261746368ULL);
        for (unsigned bits : {1u, 3u, 7u, 8u, 9u, 16u, 17u, 31u, 32u, 33u, 63u}) {
            const auto type = integer(bits);
            const uint64_t mask = (uint64_t(1) << bits) - 1;
            for (unsigned n = 0; n < 64; ++n) {
                const uint64_t a = random() & mask, b = random() & mask;
                const auto args = Json::array({constant(type, a), constant(type, b)});
                const auto signed_value = [&](uint64_t v) -> int64_t {
                    return v & (uint64_t(1) << (bits - 1)) ? -int64_t((~v & mask) + 1) : int64_t(v);
                };
                const auto sa = signed_value(a), sb = signed_value(b);
                expect_number(expression("add", type, args), (a + b) & mask);
                expect_number(expression("sub", type, args), (a - b) & mask);
                expect_number(expression("mul", type, args), (a * b) & mask);
                expect_number(expression("and", type, args), a & b);
                expect_number(expression("or", type, args), a | b);
                expect_number(expression("xor", type, args), a ^ b);
                expect_number(expression("udiv", type, args), b ? a / b : 0);
                expect_number(expression("urem", type, args), b ? a % b : 0);
                expect_number(expression("sdiv", type, args), sb ? uint64_t(sa / sb) & mask : 0);
                expect_number(expression("srem", type, args), sb ? uint64_t(sa % sb) & mask : 0);
                const auto shift = n % (bits + 2);
                const auto shift_args = Json::array({constant(type, a), constant(type, shift & mask)});
                const auto effective = shift & mask;
                expect_number(expression("shl", type, shift_args), effective < bits ? (a << effective) & mask : 0);
                expect_number(expression("lshr", type, shift_args), effective < bits ? a >> effective : 0);
                uint64_t arithmetic = effective < bits ? a >> effective : 0;
                if (effective < bits && sa < 0 && effective) arithmetic |= mask ^ (mask >> effective);
                expect_number(expression("ashr", type, shift_args), arithmetic);
                auto compare = expression("icmp", integer(1), args);
                compare["predicate"] = "slt"; expect_number(compare, sa < sb);
                compare["predicate"] = "uge"; expect_number(compare, a >= b);
                compare["predicate"] = "eq"; expect_number(compare, a == b);
            }
        }
        const auto i3 = integer(3), i9 = integer(9), i257 = integer(257);
        expect(expression("sext", i9, Json::array({constant(i3, 7)})), {255, 1});
        expect(expression("zext", i9, Json::array({constant(i3, 7)})), {7, 0});
        expect(expression("trunc", i3, Json::array({constant(i9, 511)})), {7});
        auto huge = expression("sext", i257, Json::array({constant(i3, 7)}));
        Octets negative(33, 255); negative.back() = 1;
        expect(huge, negative);
        expect(expression("add", i257, Json::array({huge, constant(i257, 1)})), Octets(33, 0));
        const auto wide = expression("shl", i257, Json::array({constant(i257, 1), constant(i257, 256)}));
        Octets wide_expected(33, 0); wide_expected.back() = 1;
        expect(wide, wide_expected);
        expect(expression("udiv", i257, Json::array({wide, constant(i257, 2)})),
            evaluate(expression("shl", i257, Json::array({constant(i257, 1), constant(i257, 255)}))));

        Json pointer = {{"kind", "pointer"}, {"bits", 64}, {"size", 8}, {"index_bits", 32}};
        const Json global = {{"kind", "symbol"}, {"type", pointer}, {"name", "global"}};
        auto gep = expression("gep", pointer, Json::array({global}));
        gep["gep"] = Json::array({{{"index", constant(i3, 1)}, {"stride", 1}}});
        expect(gep, {0, 0, 0, 0, 0x78, 0x56, 0x34, 0x12});
        gep["gep"][0]["index"] = constant(i3, 7);
        gep["gep"][0]["stride"] = 3;
        expect(gep, {252, 255, 255, 255, 0x78, 0x56, 0x34, 0x12});
        pointer = {{"kind", "pointer"}, {"bits", 16}, {"size", 2}, {"index_bits", 12}};
        gep = expression("gep", pointer, Json::array({constant(pointer, 0xafff)}));
        gep["gep"] = Json::array({{{"offset", 1}}});
        expect(gep, {0, 0xa0});
        gep["gep"] = Json::array({{{"index", constant(integer(16), 0xffff)}, {"stride", 3}}});
        expect(gep, {252, 0xaf});
        const Json pointers = {{"kind", "vector"}, {"bits", 32}, {"size", 4}, {"count", 2}, {"element", pointer}};
        const Json indices = {{"kind", "vector"}, {"bits", 18}, {"size", 3}, {"count", 2}, {"element", i9}};
        const Json pointer_values = {{"kind", "aggregate"}, {"type", pointers},
            {"elements", Json::array({constant(pointer, 0xafff), constant(pointer, 0xb002)})}};
        const Json index_values = {{"kind", "aggregate"}, {"type", indices},
            {"elements", Json::array({constant(i9, 1), constant(i9, 511)})}};
        gep = expression("gep", pointers, Json::array({pointer_values}));
        gep["gep"] = Json::array({{{"index", index_values}, {"stride", 2}}});
        expect(gep, {1, 0xa0, 0, 0xb0});
        gep["operands"][0] = constant(pointer, 0xafff);
        expect(gep, {1, 0xa0, 0xfd, 0xaf});
        expect({{"kind", "blockaddress"}, {"type", integer(32)}}, {0x78, 0x56, 0x34, 0x12});

        const Json v3 = {{"kind", "vector"}, {"bits", 9}, {"size", 2}, {"count", 3}, {"element", i3}};
        const Json vector = {{"kind", "aggregate"}, {"type", v3},
            {"elements", Json::array({constant(i3, 1), constant(i3, 2), constant(i3, 7)})}};
        expect(vector, {0xd1, 1});
        expect(expression("add", v3, Json::array({vector, vector})), {0xa2, 1});
        expect(expression("extractelement", i3, Json::array({vector, constant(integer(32), 2)})), {7});
        expect(expression("insertelement", v3, Json::array({vector, constant(i3, 4), constant(i9, 1)})), {0xe1, 1});
        auto shuffled = expression("shufflevector", v3, Json::array({vector, vector}));
        shuffled["mask"] = Json::array({2, -1, 4}); expect(shuffled, {0x87, 0});
        const Json booleans = {{"kind", "vector"}, {"bits", 3}, {"size", 1}, {"count", 3}, {"element", integer(1)}};
        expect(expression("select", v3, Json::array({constant(booleans, 5), vector, constant(v3, 511)})), {0xf9, 1});
        auto vector_compare = expression("icmp", booleans, Json::array({vector, constant(v3, 511)}));
        vector_compare["predicate"] = "sgt"; expect(vector_compare, {3});
        const Json i17 = {{"kind", "int"}, {"bits", 17}, {"size", 3}, {"alloc_size", 4}};
        const Json array = {{"kind", "array"}, {"bits", 64}, {"size", 8}, {"count", 2}, {"element", i17}};
        const Json initialized = {{"kind", "aggregate"}, {"type", array},
            {"elements", Json::array({constant(i17, 0x12345), constant(i17, 0x1abcd)})}};
        expect(initialized, {0x45, 0x23, 1, 0, 0xcd, 0xab, 1, 0});
        auto extracted = expression("extractvalue", i17, Json::array({initialized}));
        extracted["indices"] = Json::array({1}); expect(extracted, {0xcd, 0xab, 1});
        expect({{"kind", "poison"}, {"type", i9}}, {0, 0});
        rejected(expression("fadd", integer(32), Json::array({constant(integer(32), 0)})));
        rejected({{"kind", "zero"}, {"type", {{"kind", "int"}, {"size", 200001}, {"bits", 1600008}}}});
        rejected({{"kind", "bytes"}, {"type", i9}, {"bytes", Json::array({256, 0})}});
        const auto maximum = integer(1600000);
        const Json maximum_value = {{"kind", "bytes"}, {"type", maximum}, {"bytes", Octets(200000, 255)}};
        expect(expression("add", maximum, Json::array({maximum_value, constant(maximum, 1)})), Octets(200000, 0));
        std::cout << "constant evaluator: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
