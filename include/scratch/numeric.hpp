#pragma once
#include "blocks.hpp"
#include <functional>
#include <map>
#include <set>
namespace scratch {
struct NumericResult { Script code; Bytes bytes; };
class Numeric {
public:
    explicit Numeric(bool shared_helpers = false, std::string temporary_prefix = "__numeric_")
        : shared_helpers_(shared_helpers), temporary_prefix_(std::move(temporary_prefix)) {}
    NumericResult binary(const std::string& op, unsigned bits, const Bytes& lhs, const Bytes& rhs);
    // One division computes both results. Bytes are quotient followed by remainder;
    // signed division applies the original LLVM-style signs to both results.
    NumericResult divrem(unsigned bits, const Bytes& lhs, const Bytes& rhs, bool signed_op = false);
    NumericResult compare(const std::string& predicate, unsigned bits, const Bytes& lhs, const Bytes& rhs);
    NumericResult cast(const std::string& op, unsigned from_bits, unsigned to_bits, const Bytes& value);
    NumericResult intrinsic(const std::string& name, unsigned bits, const std::vector<Bytes>& args);
    void install(Project& project) const;
private:
    struct Helper {
        std::string name;
        std::vector<std::string> parameters;
        Script body;
        std::vector<std::string> outputs;
    };
    bool shared_helpers_ = false;
    bool building_helper_ = false;
    std::string temporary_prefix_;
    unsigned counter_ = 0;
    std::set<std::string> tables_;
    std::map<std::string, Helper> helpers_;
    std::string temporary(const std::string& hint);
    NumericResult shared(const std::string& signature, const std::vector<Bytes>& args,
                         const std::function<NumericResult(const std::vector<Bytes>&)>& generate);
};
}
