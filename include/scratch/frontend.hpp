#pragma once
#include "common.hpp"
#include <functional>
namespace scratch {
struct FrontendOptions {
    std::string passes;
    std::string default_layout;
    std::vector<std::string> runtime_paths;
    // Empty means serialize every definition. A nonempty list enables
    // whole-program reachability before instruction/type serialization.
    std::vector<std::string> entry_points;
    // Final executable link: internalize unused exports and run global DCE.
    // entry_points includes helper functions referenced only after IR lowering.
    bool whole_program = false;
    // Preserve LLVM source locations and basic variable location records.
    // Disabled builds retain the previous JSON schema and instruction IDs.
    bool debug_info = false;
    // Called before optimization/reachability for every nonempty inline-asm
    // template, including templates in otherwise unreachable definitions.
    std::function<void(const std::string&)> asm_validator;
};
Json read_modules(const std::vector<std::string>& paths, const FrontendOptions& options = {});
}
