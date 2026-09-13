#pragma once
#include "numeric.hpp"

namespace scratch {
// Parse and validate the entire template without types, operands or project
// state. Frontends can use this for every inline asm, including unreachable code.
void validate_assembly_template(const std::string& text);

// Lower the documented Scratch inline-assembly dialect to ordinary block AST.
// asm_info contains template, constraints and side_effects. Bytes are in the
// backend's little-endian value order, independent of memory endianness.
NumericResult emit_assembly(const Json& asm_info, const Json& return_type,
                            const std::vector<Json>& arg_types,
                            const std::vector<Bytes>& operands, Project& project);
}
