#pragma once
#include "common.hpp"

namespace scratch {
// Whole-program reachability for normalized frontend IR. Additional roots are
// useful when a later lowering introduces direct calls to runtime helpers.
// llvm.used/compiler.used, global constructors and destructors are implicit
// roots. Address-taken functions are retained conservatively if a reachable
// function contains an indirect call.
Json prune_module(Json module, const std::vector<std::string>& roots = {"main"});
}
