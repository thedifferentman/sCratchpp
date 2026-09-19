#pragma once
#include "blocks.hpp"

namespace scratch {
// Prepared resource units are independent from LLVM modules. Paths inside the
// unit are relative to its manifest; only final SVG assets enter the SB3.
void link_resources(Project& project, const std::vector<std::string>& manifests);
std::string resource_md5(const std::string& bytes);
}
