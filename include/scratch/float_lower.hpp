#pragma once
#include "common.hpp"

namespace scratch {
// Return the exact runtime wrapper roots needed by the current typed module.
// Unsupported floating formats/conversion widths fail with contextual errors.
std::vector<std::string> floating_dependencies(const Json& module);

// Lower supported floating operations to integer bit patterns, ordinary calls
// and integer instructions. Existing values/signatures/memory layouts remain
// unchanged; conversions are explicit bitcasts at operation boundaries.
Json lower_floating(Json module);
}
