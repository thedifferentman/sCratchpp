#include "scratch/prune.hpp"

#include <deque>
#include <functional>
#include <set>
#include <unordered_map>

namespace scratch {
namespace {

struct Symbol {
    std::string category;
    std::string kind;
    size_t index;
};

// Visit only normalized symbolic operands. Ordinary fields called "name"
// (including assembly fields, source names and attribute values) are not
// references and must never participate in reachability.
void walk_symbols(const Json& node, const std::function<void(const Json&)>& visit,
                  bool exclude_direct_callees = false) {
    if (node.is_array()) {
        for (const auto& child : node) walk_symbols(child, visit, exclude_direct_callees);
    } else if (node.is_object()) {
        if (node.value("kind", "") == "symbol") {
            visit(node);
            return;
        }
        if (node.value("kind", "") == "blockaddress") {
            visit(Json{{"kind", "symbol"}, {"name", node.at("function")}, {"symbol_kind", "function"}});
            return;
        }
        const bool is_call = node.value("op", "") == "call" || node.value("op", "") == "invoke" ||
                             node.value("op", "") == "callbr";
        for (auto item = node.begin(); item != node.end(); ++item) {
            if (item.key() == "debug" || item.key() == "debug_variables") continue;
            if (exclude_direct_callees && is_call && item.key() == "callee" &&
                item.value().is_object() && item.value().value("kind", "") == "symbol") continue;
            walk_symbols(item.value(), visit, exclude_direct_callees);
        }
    }
}

bool has_indirect_call(const Json& node) {
    if (node.is_array()) {
        for (const auto& child : node) if (has_indirect_call(child)) return true;
    } else if (node.is_object()) {
        const auto op = node.value("op", "");
        if ((op == "call" || op == "invoke" || op == "callbr") && node.contains("callee")) {
            const auto& callee = node.at("callee");
            if (!callee.is_object() || callee.value("kind", "") != "symbol") return true;
        }
        for (const auto& child : node) if (has_indirect_call(child)) return true;
    }
    return false;
}

} // namespace

Json prune_module(Json module, const std::vector<std::string>& roots) {
    std::unordered_map<std::string, Symbol> symbols;
    const std::vector<std::pair<std::string, std::string>> categories = {
        {"functions", "function"}, {"declarations", "function"},
        {"globals", "global"}, {"aliases", "alias"}
    };
    Json before = Json::object();
    for (const auto& category : categories) {
        if (!module.contains(category.first)) module[category.first] = Json::array();
        auto& values = module.at(category.first);
        before[category.first] = values.size();
        for (size_t i = 0; i < values.size(); ++i) {
            const auto name = values[i].at("name").get<std::string>();
            if (!symbols.emplace(name, Symbol{category.first, category.second, i}).second)
                throw Error("reachability: duplicate normalized symbol '" + name + "'");
        }
    }

    auto validate_reference = [&](const Json& reference) {
        const std::string name = reference.at("name");
        auto found = symbols.find(name);
        if (found != symbols.end() && reference.contains("symbol_kind") &&
            reference.at("symbol_kind") != found->second.kind)
            throw Error("reachability: symbol kind mismatch for '" + name + "'");
    };

    std::set<std::string> address_taken;
    for (const auto& category : categories) {
        walk_symbols(module.at(category.first), [&](const Json& reference) {
            validate_reference(reference);
            const auto name = reference.at("name").get<std::string>();
            auto found = symbols.find(name);
            if (found != symbols.end() && (found->second.kind == "function" || found->second.kind == "alias"))
                address_taken.insert(name);
        }, true);
    }
    if (module.contains("pruning") && module["pruning"].contains("address_taken_retained")) {
        // A previous conservative pass may have removed the object that made
        // a function's address visible while retaining the function itself.
        // Preserve that decision if this already-pruned module is lowered and
        // analyzed again.
        for (const auto& name : module["pruning"]["address_taken_retained"])
            if (symbols.count(name.get<std::string>())) address_taken.insert(name.get<std::string>());
    }
    if (module.contains("llvm_reachability") && module["llvm_reachability"].contains("address_taken_retained")) {
        for (const auto& name : module["llvm_reachability"]["address_taken_retained"])
            if (symbols.count(name.get<std::string>())) address_taken.insert(name.get<std::string>());
    }

    std::set<std::string> live;
    std::set<std::string> unresolved;
    std::deque<std::string> pending;
    auto mark = [&](const std::string& name) {
        if (!symbols.count(name)) { unresolved.insert(name); return; }
        if (live.insert(name).second) pending.push_back(name);
    };
    for (const auto& root : roots) mark(root);
    for (const char* root : {"llvm.global_ctors", "llvm.global_dtors", "llvm.used", "llvm.compiler.used"})
        if (symbols.count(root)) mark(root);
    if (module.contains("used_symbols")) {
        walk_symbols(module.at("used_symbols"), [&](const Json& reference) {
            validate_reference(reference);
            mark(reference.at("name").get<std::string>());
        });
    }

    bool indirect_reachable = false;
    while (!pending.empty()) {
        const auto name = pending.front(); pending.pop_front();
        const auto& symbol = symbols.at(name);
        const auto& definition = module.at(symbol.category)[symbol.index];
        walk_symbols(definition, [&](const Json& reference) {
            validate_reference(reference);
            mark(reference.at("name").get<std::string>());
        });
        if (!indirect_reachable && symbol.kind == "function" && has_indirect_call(definition)) {
            indirect_reachable = true;
            for (const auto& function : address_taken) mark(function);
        }
    }

    Json after = Json::object();
    for (const auto& category : categories) {
        Json retained = Json::array();
        for (auto& definition : module.at(category.first)) {
            if (live.count(definition.at("name").get<std::string>())) retained.push_back(std::move(definition));
        }
        after[category.first] = retained.size();
        module[category.first] = std::move(retained);
    }
    module["pruning"] = {{"roots", roots}, {"before", before}, {"after", after},
                         {"reachable_indirect_call", indirect_reachable},
                         {"address_taken_candidates", address_taken.size()},
                         {"address_taken_retained", indirect_reachable ?
                            std::vector<std::string>(address_taken.begin(), address_taken.end()) : std::vector<std::string>()},
                         {"unresolved_symbols", std::vector<std::string>(unresolved.begin(), unresolved.end())}};
    return module;
}

} // namespace scratch
