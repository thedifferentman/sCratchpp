#pragma once
#include "numeric.hpp"
#include <functional>
#include <map>

namespace scratch {

// Shared integer procedures consume direct __scl_memory list indices, not encoded
// LLVM pointers. The compiler guarantees that all participating SSA slots exist.
// Every source is snapshotted before any destination byte is changed, including
// arbitrary partial overlaps. The procedures have no user callbacks or yields:
// global temporary variables are valid until that synchronous warp call returns.
// They must not be re-entered from a concurrent execution context.
class SlotNumeric {
public:
    explicit SlotNumeric(Project& project);
    Stmt binary(const std::string& op, unsigned bits, Expr destination, Expr lhs, Expr rhs);
    // Compile-time shifts use byte displacement and one residual-bit expression,
    // without a runtime bit loop. Overshifts retain the existing zero policy.
    Stmt shift_constant(const std::string& op, unsigned bits, unsigned amount,
                        Expr destination, Expr source);
    Stmt compare(const std::string& predicate, unsigned bits, Expr destination, Expr lhs, Expr rhs);
    Stmt cast(const std::string& op, unsigned from_bits, unsigned to_bits,
              Expr destination, Expr source);
    // Widths describe stored source bytes (including one-byte intrinsic flags).
    // An overflow result is ceil(bits/8) value bytes followed by one flag byte;
    // LLVM aggregate padding remains the caller's responsibility.
    Stmt intrinsic(const std::string& name, unsigned bits, Expr destination,
                   const std::vector<Expr>& source_slots,
                   const std::vector<unsigned>& byte_lengths);
    // Installs only the lookup tables actually requested by generated algorithms.
    // Call once after all helpers have been requested; procedures are added lazily.
    void install(Project& project) const;
private:
    struct Division {
        std::string name;
        Bytes lhs;
        Bytes rhs;
        Bytes quotient;
        Bytes remainder;
    };
    Project& project_;
    Numeric numeric_{false, "__slot_numeric_tmp_"};
    unsigned counter_ = 0;
    std::map<std::string, std::string> helpers_;
    std::map<std::string, Division> divisions_;
    std::string temporary(const std::string& hint);
    Stmt operation(const std::string& signature, Expr destination,
                   const std::vector<Expr>& source_slots,
                   const std::vector<unsigned>& byte_lengths,
                   const std::function<NumericResult(const std::vector<Bytes>&)>& generate);
    const Division& division(unsigned bits, bool signed_op);
    const Division& unsigned_division_core(unsigned bits);
};

}
