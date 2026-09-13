#pragma once
#include "blocks.hpp"
namespace scratch {
struct BackendOptions { unsigned memory_size = 65536; bool debug = true; std::vector<std::string> program_args; bool debug_info = false; };
Project compile(const Json& module, const BackendOptions& options = {});
}
