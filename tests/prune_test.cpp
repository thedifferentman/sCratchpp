#include "scratch/prune.hpp"
#include <iostream>
#include <set>

using namespace scratch;

static void require(bool condition, const char* message) {
    if (!condition) throw Error(message);
}
static Json symbol(const std::string& name, const std::string& kind) {
    return {{"kind", "symbol"}, {"name", name}, {"symbol_kind", kind}};
}
static Json call(const std::string& name) {
    return {{"op", "call"}, {"callee", symbol(name, "function")}, {"operands", Json::array()}};
}
static Json function(const std::string& name, Json instructions = Json::array()) {
    return {{"name", name}, {"blocks", Json::array({{{"id", "b0"}, {"instructions", instructions}}})}};
}
static std::set<std::string> names(const Json& module, const std::string& category) {
    std::set<std::string> result;
    for (const auto& item : module.at(category)) result.insert(item.at("name").get<std::string>());
    return result;
}

int main() {
    try {
        Json input = {
            {"functions", Json::array({
                function("main", Json::array({call("helper"), {{"op", "load"}, {"operands", Json::array({symbol("data", "global")})}}})),
                function("helper"), function("alias_target"), function("ctor", Json::array({call("ctor_dependency")})),
                function("ctor_dependency"), function("dtor"), function("used_function"), function("runtime_helper"),
                function("dead", Json::array({call("only_direct_from_dead")})), function("only_direct_from_dead"),
                function("address_taken_in_dead_global")
            })},
            {"declarations", Json::array()},
            {"globals", Json::array({
                {{"name", "data"}, {"initializer", symbol("function_alias", "alias")}},
                {{"name", "llvm.global_ctors"}, {"initializer", Json::array({symbol("ctor", "function")})}},
                {{"name", "llvm.global_dtors"}, {"initializer", Json::array({symbol("dtor", "function")})}},
                {{"name", "used_global"}, {"initializer", symbol("used_function", "function")}},
                {{"name", "dead_global"}, {"initializer", symbol("address_taken_in_dead_global", "function")}}
            })},
            {"aliases", Json::array({{{"name", "function_alias"}, {"value", symbol("alias_target", "function")}}})},
            {"used_symbols", Json::array({symbol("used_global", "global")})}
        };
        auto direct = prune_module(input, {"main", "runtime_helper"});
        require(names(direct, "functions") == std::set<std::string>({"main", "helper", "alias_target", "ctor",
            "ctor_dependency", "dtor", "used_function", "runtime_helper"}), "direct call/root reachability failed");
        require(names(direct, "globals") == std::set<std::string>({"data", "llvm.global_ctors", "llvm.global_dtors", "used_global"}),
            "global initializer/used reachability failed");
        require(names(direct, "aliases") == std::set<std::string>({"function_alias"}), "alias was not retained");
        require(!direct["pruning"]["reachable_indirect_call"].get<bool>(), "direct call was mistaken for an indirect call");

        input["functions"][0]["blocks"][0]["instructions"].push_back({{"op", "call"},
            {"callee", {{"kind", "ref"}, {"id", "fnptr"}}}, {"operands", Json::array()}});
        auto indirect = prune_module(input);
        require(names(indirect, "functions").count("address_taken_in_dead_global") == 1,
            "indirect call did not retain address-taken candidate");
        require(names(indirect, "functions").count("only_direct_from_dead") == 0,
            "direct references in dead functions should not become address-taken roots");
        require(names(indirect, "globals").count("dead_global") == 0,
            "an address-taken target need not retain its unused source global");
        require(indirect["pruning"]["reachable_indirect_call"].get<bool>(), "indirect call was not recognized");
        auto twice = prune_module(indirect);
        for (const char* category : {"functions", "globals", "aliases", "declarations"})
            require(twice[category] == indirect[category], "pruning should be idempotent");

        auto malformed = input;
        malformed["functions"][0]["blocks"][0]["instructions"][1]["operands"][0]["symbol_kind"] = "function";
        bool rejected = false;
        try { (void)prune_module(malformed); } catch (const Error&) { rejected = true; }
        require(rejected, "function/global symbol-kind mismatch was accepted");

        std::cout << "prune tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
