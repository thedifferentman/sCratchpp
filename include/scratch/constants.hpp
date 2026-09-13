#pragma once

#include "scratch/common.hpp"
#include <functional>

namespace scratch {

// Evaluate a normalized LLVM constant after symbols have received addresses.
// Bytes are little-endian and always match constant.type.size, including layout
// padding. The resolver handles complete symbol/blockaddress operands (including
// symbol addends and alias recursion). No pointer dereference checks are made.
// Undef/poison, invalid vector indices, oversized shifts and division by zero
// use the same deterministic zero representation as the existing backend.
// Unsupported expressions produce Error; they never fall back to imprecise host
// floating arithmetic or generated Scratch arithmetic.
std::vector<unsigned> evaluate_constant(
    const Json& constant,
    const std::function<std::vector<unsigned>(const Json&)>& resolve_symbol);

}
