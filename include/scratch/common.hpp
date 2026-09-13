#pragma once
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace scratch {
using Json = nlohmann::ordered_json;
using Expr = Json;
using Stmt = Json;
using Script = std::vector<Stmt>;
using Bytes = std::vector<Expr>;
struct Error : std::runtime_error { using std::runtime_error::runtime_error; };
}
