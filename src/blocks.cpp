#include "scratch/blocks.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <unordered_map>

namespace scratch {
namespace {
std::string symbol_id(char kind, const std::string& name) {
    static const char hex[] = "0123456789abcdef";
    std::string id(1, kind);
    for (unsigned char c : name) { id += hex[c >> 4]; id += hex[c & 15]; }
    return id;
}
Json field(const std::string& kind, const std::string& name) {
    return Json{{kind, Json::array({name, symbol_id(kind == "VARIABLE" ? 'v' : 'l', name)})}};
}
Expr binary(const std::string& op, Expr a, Expr b, bool number) {
    return expr("operator_" + op, Json{{number ? "NUM1" : "OPERAND1", std::move(a)},
                                     {number ? "NUM2" : "OPERAND2", std::move(b)}});
}
bool boolean_opcode(const std::string& op) {
    static const std::set<std::string> names = {
        "operator_equals", "operator_lt", "operator_gt", "operator_and", "operator_or", "operator_not",
        "operator_contains", "data_listcontainsitem", "argument_reporter_boolean", "sensing_touchingobject",
        "sensing_touchingcolor", "sensing_coloristouchingcolor", "sensing_keypressed", "sensing_mousedown"};
    return names.count(op) != 0;
}
std::string literal(const Json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_null()) return "";
    if (value.is_boolean()) return value.get<bool>() ? "1" : "0";
    if (value.is_number()) return value.dump();
    throw Error("Scratch input must be a scalar or an AST expression");
}
struct ProcInfo {
    std::string code;
    std::vector<std::string> params;
    std::vector<std::string> ids;
};
Json mutation(const ProcInfo& proc, bool prototype) {
    Json result{{"tagName", "mutation"}, {"children", Json::array()}, {"proccode", proc.code},
                {"argumentids", Json(proc.ids).dump()}, {"warp", "true"}};
    if (prototype) {
        result["argumentnames"] = Json(proc.params).dump();
        result["argumentdefaults"] = Json(std::vector<std::string>(proc.params.size(), "")).dump();
    }
    return result;
}
class Builder {
public:
    Json variables = Json::object();
    Json lists = Json::object();
    Json extensions = Json::array();
    Json debug_points = Json::object();
    std::map<std::string, ProcInfo> procedures;

    Builder(const Json& initial_variables, const Json& initial_lists, const Json& initial_extensions,
            const std::set<std::string>& readonly_lists)
        : initial_variables_(initial_variables), initial_lists_(initial_lists), readonly_lists_(readonly_lists) {
        if (!initial_variables.is_object() || !initial_lists.is_object() || !initial_extensions.is_array())
            throw Error("Project variables/lists must be objects and extensions must be an array");
        for (const auto& entry : initial_variables.items()) {
            if (entry.value().is_structured()) throw Error("Initial variable '" + entry.key() + "' must be a scalar");
            declare("VARIABLE", entry.key(), symbol_id('v', entry.key()));
        }
        for (const auto& entry : initial_lists.items()) {
            if (!entry.value().is_array()) throw Error("Initial list '" + entry.key() + "' must be an array");
            for (const auto& value : entry.value())
                if (value.is_structured()) throw Error("Initial list '" + entry.key() + "' contains a non-scalar item");
            declare("LIST", entry.key(), symbol_id('l', entry.key()));
        }
        for (const auto& extension : initial_extensions) {
            if (!extension.is_string()) throw Error("Extension IDs must be strings");
            extension_used(extension.get<std::string>());
        }
    }

    void register_procedure(const std::string& name, const std::vector<std::string>& params) {
        if (procedures.count(name)) throw Error("Duplicate Scratch procedure: " + name);
        if (name.find("%s") != std::string::npos || name.find("%b") != std::string::npos ||
            name.find("%n") != std::string::npos)
            throw Error("Scratch procedure name contains a reserved argument placeholder: " + name);
        std::set<std::string> seen;
        ProcInfo info{name, params, {}};
        for (std::size_t i = 0; i < params.size(); ++i) {
            if (!seen.insert(params[i]).second) throw Error("Duplicate argument name in procedure: " + name);
            info.code += " %s";
            info.ids.push_back(symbol_id('a', name) + "_" + std::to_string(i));
        }
        procedures.emplace(name, std::move(info));
    }
    std::string block(const std::string& opcode, const Json& parent, bool shadow = false, bool top = false) {
        const auto id = "b" + std::to_string(++counter_);
        block_indices_.emplace(id, block_nodes_.size());
        block_nodes_.push_back(Json{{"opcode", opcode}, {"next", nullptr}, {"parent", parent},
                          {"inputs", Json::object()}, {"fields", Json::object()},
                          {"shadow", shadow}, {"topLevel", top}});
        if (opcode.rfind("pen_", 0) == 0) extension_used("pen");
        return id;
    }
    void procedure(const std::string& name, const Script& body, std::size_t index) {
        const auto& info = procedures.at(name);
        const auto definition = block("procedures_definition", nullptr, false, true);
        node(definition)["x"] = 420 * (index % 3);
        node(definition)["y"] = 200 + 220 * (index / 3);
        const auto prototype = block("procedures_prototype", definition, true);
        node(definition)["inputs"]["custom_block"] = Json::array({1, prototype});
        node(prototype)["mutation"] = mutation(info, true);
        for (std::size_t i = 0; i < info.params.size(); ++i) {
            const auto reporter = block("argument_reporter_string_number", prototype, true);
            node(reporter)["fields"]["VALUE"] = Json::array({info.params[i], nullptr});
            node(prototype)["inputs"][info.ids[i]] = Json::array({1, reporter});
        }
        node(definition)["next"] = sequence(body, definition);
    }
    void hat(const Script& body, std::size_t index) {
        const auto id = block("event_whenflagclicked", nullptr, false, true);
        node(id)["x"] = 420 * index;
        node(id)["y"] = 0;
        node(id)["next"] = sequence(body, id);
    }
    void key_hat(const std::string& key, const Script& body, std::size_t index) {
        const auto id=block("event_whenkeypressed",nullptr,false,true);
        node(id)["x"]=420*index;node(id)["y"]=0;
        node(id)["fields"]["KEY_OPTION"]=Json::array({key,nullptr});
        node(id)["next"]=sequence(body,id);
    }
    void validate() const {
        // Validate references and their ownership before emitting an archive. This catches
        // broken parent/next links even when a tolerant VM could otherwise load the file.
        for (std::size_t index = 0; index < block_nodes_.size(); ++index) {
            const auto id = "b" + std::to_string(index + 1);
            const auto& node = block_nodes_[index];
            if (!node["next"].is_null()) validate_child(id, node["next"]);
            for (const auto& input : node["inputs"].items()) {
                const auto& data = input.value();
                if (!data.is_array() || data.size() < 2) throw Error("Malformed input on block " + id);
                if (data[1].is_string()) validate_child(id, data[1]);
                if (data.size() > 2 && data[2].is_string()) validate_child(id, data[2]);
            }
            if (node["topLevel"].get<bool>() && !node["parent"].is_null())
                throw Error("Top-level block has a parent: " + id);
            if (!node["topLevel"].get<bool>() && node["parent"].is_null())
                throw Error("Non-top-level block has no parent: " + id);
        }
    }
    Json take_blocks() {
        Json result = Json::object();
        // ordered_json's keyed insertion scans earlier keys. IDs are already unique,
        // so append directly to its ordered map to keep large projects linear.
        auto& object = result.get_ref<Json::object_t&>();
        object.reserve(block_nodes_.size());
        for (std::size_t i = 0; i < block_nodes_.size(); ++i)
            object.emplace_back("b" + std::to_string(i + 1), std::move(block_nodes_[i]));
        return result;
    }

private:
    Json initial_variables_;
    Json initial_lists_;
    const std::set<std::string>& readonly_lists_;
    std::size_t counter_ = 0;
    std::vector<Json> block_nodes_;
    std::unordered_map<std::string, std::size_t> block_indices_;

    Json& node(const std::string& id) { return block_nodes_.at(block_indices_.at(id)); }

    void extension_used(const std::string& name) {
        if (std::find(extensions.begin(), extensions.end(), name) == extensions.end()) extensions.push_back(name);
    }
    void declare(const std::string& type, const std::string& name, const std::string& id) {
        Json& declarations = type == "VARIABLE" ? variables : lists;
        const Json& initial = type == "VARIABLE" ? initial_variables_ : initial_lists_;
        if (declarations.contains(id)) {
            if (declarations[id][0] != name) throw Error("Conflicting Scratch symbol ID: " + id);
            return;
        }
        const Json value = initial.contains(name) ? initial[name] : (type == "VARIABLE" ? Json(0) : Json::array());
        declarations[id] = Json::array({name, value});
    }
    Json normalize_fields(Json fields) {
        if (!fields.is_object()) throw Error("Scratch AST fields must be an object");
        for (auto& entry : fields.items()) {
            if (!entry.value().is_array()) entry.value() = Json::array({entry.value(), nullptr});
            if (entry.value().size() != 2) throw Error("Scratch fields must contain [value, id]");
            if (entry.key() == "VARIABLE" || entry.key() == "LIST") {
                if (!entry.value()[0].is_string()) throw Error("Scratch variable/list names must be strings");
                const auto name = entry.value()[0].get<std::string>();
                if (entry.value()[1].is_null())
                    entry.value()[1] = symbol_id(entry.key() == "VARIABLE" ? 'v' : 'l', name);
                if (!entry.value()[1].is_string()) throw Error("Scratch variable/list IDs must be strings");
                declare(entry.key(), name, entry.value()[1].get<std::string>());
            }
        }
        return fields;
    }
    Json input(const Expr& value, const std::string& parent, bool boolean_socket = false) {
        // LLVM predicates and runtime flags use numeric zero/nonzero values.
        // The VM accepts them directly, but Blockly rejects their reporter
        // shape in Boolean sockets. Make the numeric test explicit.
        if (boolean_socket && (!value.is_object() || !boolean_opcode(value.value("op", ""))))
            return input(lnot(eq(value, 0)), parent);
        if (!value.is_object())
            return Json::array({1, Json::array({value.is_number() || value.is_boolean() ? 4 : 10, literal(value)})});
        const auto id = ast_block(value, parent);
        const auto op = value.at("op").get<std::string>();
        if (value.value("shadow", false)) return Json::array({1, id});
        if (boolean_opcode(op)) return Json::array({2, id});
        return Json::array({3, id, Json::array({4, ""})});
    }
    std::string ast_block(const Json& ast, const std::string& parent) {
        if (!ast.is_object() || !ast.contains("op") || !ast["op"].is_string())
            throw Error("Scratch AST node must have a string op");
        const auto op = ast["op"].get<std::string>();
        const auto id = block(op, parent, ast.value("shadow", false));
        if (ast.contains("debug_point")) debug_points[id] = ast.at("debug_point");
        node(id)["fields"] = normalize_fields(ast.value("fields", Json::object()));
        static const std::set<std::string> list_writes={"data_addtolist","data_deleteoflist","data_deletealloflist","data_insertatlist","data_replaceitemoflist"};
        if(list_writes.count(op) && node(id)["fields"].contains("LIST") &&
           readonly_lists_.count(node(id)["fields"]["LIST"][0].get<std::string>()))
            throw Error("Read-only resource list cannot be modified: "+node(id)["fields"]["LIST"][0].get<std::string>());
        if (op == "procedures_call" && ast.contains("callee")) {
            const auto name = ast.at("callee").get<std::string>();
            const auto found = procedures.find(name);
            if (found == procedures.end()) throw Error("Undefined Scratch procedure: " + name);
            const auto& info = found->second;
            const auto& args = ast.at("args");
            if (!args.is_array() || args.size() != info.params.size()) throw Error("Argument count mismatch calling: " + name);
            node(id)["mutation"] = mutation(info, false);
            for (std::size_t i = 0; i < args.size(); ++i) {
                const auto serialized = input(args[i], id);
                node(id)["inputs"][info.ids[i]] = serialized;
            }
        } else {
            const auto inputs = ast.value("inputs", Json::object());
            if (!inputs.is_object()) throw Error("Scratch AST inputs must be an object");
            for (const auto& entry : inputs.items()) {
                const bool boolean_socket =
                    (entry.key() == "CONDITION" && (op == "control_if" || op == "control_if_else" ||
                        op == "control_repeat_until" || op == "control_wait_until")) ||
                    ((op == "operator_and" || op == "operator_or") &&
                        (entry.key() == "OPERAND1" || entry.key() == "OPERAND2")) ||
                    (op == "operator_not" && entry.key() == "OPERAND");
                const auto serialized = input(entry.value(), id, boolean_socket);
                node(id)["inputs"][entry.key()] = serialized;
            }
            if (ast.contains("mutation")) node(id)["mutation"] = ast["mutation"];
        }
        if (ast.contains("branches")) {
            if (!ast["branches"].is_object()) throw Error("Scratch AST branches must be an object");
            for (const auto& branch : ast["branches"].items()) {
                if (!branch.value().is_array()) throw Error("Scratch branch must be an array of statements");
                const auto start = sequence(branch.value().get<Script>(), id);
                if (!start.is_null()) node(id)["inputs"][branch.key()] = Json::array({2, start});
            }
        }
        return id;
    }
    Json sequence(const Script& statements, const std::string& parent) {
        Json first = nullptr;
        std::string previous;
        for (const auto& ast : statements) {
            const auto id = ast_block(ast, previous.empty() ? parent : previous);
            if (previous.empty()) first = id;
            else node(previous)["next"] = id;
            previous = id;
        }
        return first;
    }
    void validate_child(const std::string& parent, const Json& child) const {
        if (!child.is_string() || !block_indices_.count(child.get<std::string>())) throw Error("Dangling Scratch block reference");
        if (block_nodes_.at(block_indices_.at(child.get<std::string>())).at("parent") != parent)
            throw Error("Incorrect Scratch block parent: " + child.get<std::string>());
    }
};

constexpr const char* costume_id = "7ba51fc185139c2c7fbb43cb3023dafd";
constexpr const char* costume_svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1\" height=\"1\" viewBox=\"0 0 1 1\"></svg>";
Json target(bool stage) {
    Json result{{"isStage", stage}, {"name", stage ? "Stage" : "Program"},
        {"variables", Json::object()}, {"lists", Json::object()}, {"broadcasts", Json::object()},
        {"blocks", Json::object()}, {"comments", Json::object()}, {"currentCostume", 0},
        {"costumes", Json::array({Json{{"assetId", costume_id}, {"name", "blank"}, {"bitmapResolution", 1},
            {"md5ext", std::string(costume_id) + ".svg"}, {"dataFormat", "svg"},
            {"rotationCenterX", 0.5}, {"rotationCenterY", 0.5}}})},
        {"sounds", Json::array()}, {"volume", 100}, {"layerOrder", stage ? 0 : 1}};
    if (stage) {
        result["tempo"] = 60; result["videoTransparency"] = 50; result["videoState"] = "off";
        result["textToSpeechLanguage"] = nullptr;
    } else {
        result["visible"] = false; result["x"] = 0; result["y"] = 0; result["size"] = 100;
        result["direction"] = 90; result["draggable"] = false; result["rotationStyle"] = "all around";
    }
    return result;
}
std::uint32_t crc32(const std::string& data) {
    std::uint32_t crc = 0xffffffffu;
    for (unsigned char byte : data) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
void le16(std::ostream& out, std::uint16_t value) {
    out.put(static_cast<char>(value & 255)); out.put(static_cast<char>(value >> 8));
}
void le32(std::ostream& out, std::uint32_t value) {
    le16(out, static_cast<std::uint16_t>(value)); le16(out, static_cast<std::uint16_t>(value >> 16));
}
std::uint32_t zip_position(std::ostream& out) {
    const auto position = out.tellp();
    if (position < 0 || static_cast<std::uint64_t>(position) > std::numeric_limits<std::uint32_t>::max())
        throw Error("SB3 archive exceeds the supported ZIP32 size");
    return static_cast<std::uint32_t>(position);
}
void write_zip(std::ostream& out, const std::vector<std::pair<std::string, std::string>>& files) {
    struct Entry { std::string name; std::uint32_t size; std::uint32_t crc; std::uint32_t offset; };
    std::vector<Entry> entries;
    if (files.size() > 65535) throw Error("Too many files for ZIP32 archive");
    for (const auto& file : files) {
        if (file.first.size() > 65535 || file.second.size() > std::numeric_limits<std::uint32_t>::max())
            throw Error("SB3 member exceeds the supported ZIP32 size");
        Entry entry{file.first, static_cast<std::uint32_t>(file.second.size()), crc32(file.second), zip_position(out)};
        entries.push_back(entry);
        le32(out, 0x04034b50); le16(out, 20); le16(out, 0x0800); le16(out, 0); // UTF-8, stored
        le16(out, 0); le16(out, 33); // Fixed 1980-01-01 timestamp makes archives deterministic.
        le32(out, entry.crc); le32(out, entry.size); le32(out, entry.size);
        le16(out, static_cast<std::uint16_t>(entry.name.size())); le16(out, 0);
        out.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
        out.write(file.second.data(), static_cast<std::streamsize>(file.second.size()));
    }
    const auto central_start = zip_position(out);
    for (const auto& entry : entries) {
        le32(out, 0x02014b50); le16(out, 20); le16(out, 20); le16(out, 0x0800); le16(out, 0);
        le16(out, 0); le16(out, 33); le32(out, entry.crc); le32(out, entry.size); le32(out, entry.size);
        le16(out, static_cast<std::uint16_t>(entry.name.size())); le16(out, 0); le16(out, 0);
        le16(out, 0); le16(out, 0); le32(out, 0); le32(out, entry.offset);
        out.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
    }
    const auto central_size = zip_position(out) - central_start;
    le32(out, 0x06054b50); le16(out, 0); le16(out, 0);
    le16(out, static_cast<std::uint16_t>(entries.size())); le16(out, static_cast<std::uint16_t>(entries.size()));
    le32(out, central_size); le32(out, central_start); le16(out, 0);
    if (!out) throw Error("Failed to write SB3 archive");
}
void append_extra_files(std::vector<std::pair<std::string, std::string>>& members, const Json& files) {
    if (!files.is_object()) throw Error("Project files must map archive names to UTF-8 text");
    std::set<std::string> names;
    for (const auto& member : members) names.insert(member.first);
    for (const auto& file : files.items()) {
        const auto& name = file.key();
        if (name.empty() || name.front() == '/' || name.back() == '/' ||
            name.find('\\') != std::string::npos || name.find(':') != std::string::npos ||
            name.find('\0') != std::string::npos)
            throw Error("Invalid SB3 member path: " + name);
        std::size_t start = 0;
        while (start < name.size()) {
            const auto end = name.find('/', start);
            const auto component = name.substr(start, end == std::string::npos ? end : end - start);
            if (component.empty() || component == "." || component == "..")
                throw Error("Invalid SB3 member path: " + name);
            if (end == std::string::npos) break;
            start = end + 1;
        }
        if (!names.insert(name).second) throw Error("Duplicate SB3 member: " + name);
        if (!file.value().is_string()) throw Error("SB3 extra file contents must be UTF-8 text: " + name);
        members.emplace_back(name, file.value().get<std::string>());
    }
}
} // namespace

Expr expr(const std::string& opcode, const Json& inputs, const Json& fields) {
    return Json{{"op", opcode}, {"inputs", inputs}, {"fields", fields}};
}
Stmt stmt(const std::string& opcode, const Json& inputs, const Json& fields) { return expr(opcode, inputs, fields); }
Expr var(const std::string& name) { return expr("data_variable", Json::object(), field("VARIABLE", name)); }
Expr arg(const std::string& name) {
    return expr("argument_reporter_string_number", Json::object(), Json{{"VALUE", Json::array({name, nullptr})}});
}
Expr add(Expr a, Expr b) { return binary("add", std::move(a), std::move(b), true); }
Expr sub(Expr a, Expr b) { return binary("subtract", std::move(a), std::move(b), true); }
Expr mul(Expr a, Expr b) { return binary("multiply", std::move(a), std::move(b), true); }
Expr div(Expr a, Expr b) { return binary("divide", std::move(a), std::move(b), true); }
Expr mod(Expr a, Expr b) { return binary("mod", std::move(a), std::move(b), true); }
Expr eq(Expr a, Expr b) { return binary("equals", std::move(a), std::move(b), false); }
Expr lt(Expr a, Expr b) { return binary("lt", std::move(a), std::move(b), false); }
Expr gt(Expr a, Expr b) { return binary("gt", std::move(a), std::move(b), false); }
Expr land(Expr a, Expr b) { return binary("and", std::move(a), std::move(b), false); }
Expr lor(Expr a, Expr b) { return binary("or", std::move(a), std::move(b), false); }
Expr lnot(Expr a) { return expr("operator_not", Json{{"OPERAND", std::move(a)}}); }
Expr floor_(Expr a) { return expr("operator_mathop", Json{{"NUM", std::move(a)}}, Json{{"OPERATOR", Json::array({"floor", nullptr})}}); }
Expr item(const std::string& list, Expr index) { return expr("data_itemoflist", Json{{"INDEX", std::move(index)}}, field("LIST", list)); }
Stmt set(const std::string& name, Expr value) { return stmt("data_setvariableto", Json{{"VALUE", std::move(value)}}, field("VARIABLE", name)); }
Stmt replace(const std::string& list, Expr index, Expr value) {
    return stmt("data_replaceitemoflist", Json{{"INDEX", std::move(index)}, {"ITEM", std::move(value)}}, field("LIST", list));
}
Stmt append(const std::string& list, Expr value) { return stmt("data_addtolist", Json{{"ITEM", std::move(value)}}, field("LIST", list)); }
Stmt clear(const std::string& list) { return stmt("data_deletealloflist", Json::object(), field("LIST", list)); }
Stmt iff(Expr condition, Script yes, Script no) {
    auto result = stmt(no.empty() ? "control_if" : "control_if_else", Json{{"CONDITION", std::move(condition)}});
    result["branches"] = Json{{"SUBSTACK", std::move(yes)}};
    if (!no.empty()) result["branches"]["SUBSTACK2"] = std::move(no);
    return result;
}
Stmt repeat(Expr count, Script body) {
    auto result = stmt("control_repeat", Json{{"TIMES", std::move(count)}});
    result["branches"] = Json{{"SUBSTACK", std::move(body)}}; return result;
}
Stmt until(Expr condition, Script body) {
    auto result = stmt("control_repeat_until", Json{{"CONDITION", std::move(condition)}});
    result["branches"] = Json{{"SUBSTACK", std::move(body)}}; return result;
}
Stmt call(const std::string& name, const std::vector<Expr>& args) {
    return Json{{"op", "procedures_call"}, {"callee", name}, {"args", args}};
}
void extend(Script& dst, const Script& src) { dst.insert(dst.end(), src.begin(), src.end()); }
void Project::procedure(const std::string& name, const std::vector<std::string>& params, Script body) {
    procedures_.push_back(Procedure{name, params, std::move(body)});
}
void Project::green_flag(Script body) { hats_.push_back(std::move(body)); }
Json Project::build() {
    Builder builder(variables, lists, extensions,readonly_lists);
    for (const auto& p : procedures_) builder.register_procedure(p.name, p.params);
    struct Collector { std::string name; Script body; };
    struct KeyHat { std::string key; Script body; };
    std::vector<Collector> collectors;
    std::vector<KeyHat> key_hats;
    Script event_startup;
    for(size_t i=0;i<resource_events.size();++i) {
        const auto& event=resource_events[i];
        const std::string type=event.at("type"), queue=event.at("queue"), enabled=event.at("enabled");
        const std::string name="__scl_event_"+type+"_"+std::to_string(i);
        const auto dropped=enabled+"_dropped";
        builder.register_procedure(name,{"code","key"});
        auto active=land(eq(var(enabled),1),eq(var("__scl_status"),"running"));
        auto pressed=expr("sensing_keypressed",{{"KEY_OPTION",arg("key")}});
        Json accept;
        if(type=="wheel") {
            accept=lnot(pressed);
            key_hats.push_back({"up arrow",{call(name,{1,"up arrow"})}});
            key_hats.push_back({"down arrow",{call(name,{-1,"down arrow"})}});
        } else if(type=="keyboard") {
            // Wheel IO also fires up/down hats. Only those two keys need a
            // live-state filter. Other hats retain short presses after release.
            accept=lor(lnot(lor(eq(arg("code"),0x110001),eq(arg("code"),0x110003))),pressed);
            auto add_key=[&](const std::string& key,int code) {
                key_hats.push_back({key,{call(name,{code,key})}});
            };
            for(int code=33;code<=126;++code) {
                if(code>='a' && code<='z')continue; // Scratch folds case.
                add_key(std::string(1,static_cast<char>(code)),code);
            }
            add_key("space",32);add_key("enter",13);
            add_key("left arrow",0x110000);add_key("up arrow",0x110001);
            add_key("right arrow",0x110002);add_key("down arrow",0x110003);
            // These standard hat fields are recognized by TurboWarp. Vanilla
            // Scratch never fires them; no extension or extra sprite is used.
            add_key("backspace",8);add_key("delete",127);add_key("escape",27);
            add_key("shift",0x110004);add_key("caps lock",0x110005);
            add_key("scroll lock",0x110006);add_key("control",0x110007);
            add_key("insert",0x110008);add_key("home",0x110009);add_key("end",0x11000a);
            add_key("page up",0x11000b);add_key("page down",0x11000c);
        } else throw Error("Unknown resource event type: "+type);
        auto room=lt(expr("data_lengthoflist",Json::object(),{{"LIST",queue}}),event.at("capacity"));
        Script enqueue{append(queue,arg("code"))};
        if(type=="keyboard") {
            const auto value=name+"_value";
            auto tw=expr("argument_reporter_boolean",Json::object(),{{"VALUE","is turbowarp?"}});
            auto shift=expr("sensing_keypressed",{{"KEY_OPTION","shift"}});
            enqueue={set(value,arg("code")),
                // Sample Shift on the native collector thread, before the
                // event waits in the queue. Caps Lock is deliberately ignored.
                iff(land(tw,land(land(gt(arg("code"),64),lt(arg("code"),91)),lnot(shift))),
                    {set(value,add(arg("code"),32))}),append(queue,var(value))};
        }
        collectors.push_back({name,{iff(land(active,accept),{iff(room,enqueue,
            {stmt("data_changevariableby",{{"VALUE",1}},{{"VARIABLE",dropped}})})})}});
        event_startup.push_back(set(enabled,0));event_startup.push_back(set(dropped,0));event_startup.push_back(clear(queue));
    }
    for (std::size_t i = 0; i < hats_.size(); ++i) {
        if(costumes.empty() && event_startup.empty())builder.hat(hats_[i], i);
        else {
            // A second green flag must not reuse the previous run's costume.
            // Explicit arithmetic forces a numeric index even in vanilla VM.
            Script startup=event_startup;
            if(!costumes.empty())extend(startup,{stmt("looks_hide"),stmt("looks_switchcostumeto",{{"COSTUME",add(1,0)}})});
            extend(startup,hats_[i]);builder.hat(startup,i);
        }
    }
    for (std::size_t i = 0; i < procedures_.size(); ++i) builder.procedure(procedures_[i].name, procedures_[i].body, i);
    for(size_t i=0;i<key_hats.size();++i)
        builder.key_hat(key_hats[i].key,key_hats[i].body,hats_.size()+i);
    for(size_t i=0;i<collectors.size();++i)
        builder.procedure(collectors[i].name,collectors[i].body,procedures_.size()+i);
    builder.validate();
    auto stage = target(true);
    Json settings{{"framerate",60},{"high_quality_pen",true},{"offscreen_sprites",true},
        {"interpolation",false},{"unlimited_clones",false},{"remove_limits",false},
        {"stage_width",480},{"stage_height",360}};
    if (!turbowarp_settings.is_object()) throw Error("turbowarp settings must be an object");
    for (auto it=turbowarp_settings.begin();it!=turbowarp_settings.end();++it) {
        if (!settings.contains(it.key())) throw Error("unsupported turbowarp setting: "+it.key());
        settings[it.key()]=it.value();
    }
    for (const auto* key : {"high_quality_pen","offscreen_sprites","interpolation","unlimited_clones","remove_limits"})
        if (!settings[key].is_boolean()) throw Error(std::string("turbowarp.")+key+" must be boolean");
    if (!settings["framerate"].is_number() || !(settings["framerate"].get<double>()>=0 && settings["framerate"].get<double>()<=250))
        throw Error("turbowarp.framerate must be a number between 0 and 250 (0 uses the display refresh rate)");
    for (const auto* key : {"stage_width","stage_height"})
        if (!settings[key].is_number_integer() || settings[key].get<double>()<1 || settings[key].get<double>()>8192)
            throw Error(std::string("turbowarp.")+key+" must be an integer between 1 and 8192");
    Json options{{"framerate",settings["framerate"]},{"hq",settings["high_quality_pen"]},
        {"interpolation",settings["interpolation"]},{"turbo",false},
        {"width",settings["stage_width"]},{"height",settings["stage_height"]},
        {"runtimeOptions",Json{{"fencing",!settings["offscreen_sprites"].get<bool>()},
            {"miscLimits",!settings["remove_limits"].get<bool>()},{"maxClones",300}}}};
    // TurboWarp uses extended JSON with a literal Infinity for unlimited clones.
    if (settings["unlimited_clones"].get<bool>()) options["runtimeOptions"]["maxClones"]="__tw_infinity__";
    auto options_text=options.dump();
    const auto infinity=options_text.find("\"__tw_infinity__\"");
    if (infinity!=std::string::npos) options_text.replace(infinity,std::string("\"__tw_infinity__\"").size(),"Infinity");
    stage["comments"]["scrpp_tw_settings"]=Json{{"blockId",nullptr},{"x",50},{"y",50},
        {"width",350},{"height",170},{"minimized",true},
        {"text","Configuration for https://turbowarp.org/\nGenerated by sCr++; configure in sCrpp.toml.\n"+options_text+" // _twconfig_"}};
    auto sprite = target(false);
    for(const auto& costume:costumes)sprite["costumes"].push_back(costume);
    sprite["variables"] = std::move(builder.variables);
    sprite["lists"] = std::move(builder.lists);
    sprite["blocks"] = builder.take_blocks();
    Json result{{"targets", Json::array({std::move(stage), std::move(sprite)})}, {"monitors", Json::array()},
                {"extensions", std::move(builder.extensions)},
                {"meta", Json{{"semver", "3.0.0"}, {"vm", "11.0.0"}, {"agent", "s-C-ratch++"}}}};
    if (!debug_map.empty()) {
        debug_map["points"] = std::move(builder.debug_points);
        const auto checksum = crc32(result.dump());
        static const char hex[] = "0123456789abcdef";
        std::string fingerprint(8, '0');
        for (unsigned i = 0; i < 8; ++i) fingerprint[7-i] = hex[(checksum >> (i*4)) & 15];
        debug_map["projectCrc32"] = fingerprint;
    }
    return result;
}
void Project::save(const std::string& path) {
    const auto output = std::filesystem::u8path(path);
    const auto project = build();
    auto extension = output.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::vector<std::pair<std::string, std::string>> members;
    if (extension != ".json") {
        members = {{"project.json", project.dump()}, {std::string(costume_id) + ".svg", costume_svg}};
        for(const auto& asset:assets) {
            // Only content-addressed SVG names are admitted by the resource
            // linker. Keep the public builder API from introducing ZIP paths.
            if(asset.first.size()!=36 || asset.first.substr(32)!=".svg" ||
               !std::all_of(asset.first.begin(),asset.first.begin()+32,[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))
                throw Error("Invalid resource archive name: "+asset.first);
            const auto existing=std::find_if(members.begin(),members.end(),[&](const auto& member){return member.first==asset.first;});
            if(existing!=members.end()) {
                if(existing->second!=asset.second)throw Error("Conflicting resource archive member: "+asset.first);
            }else members.push_back(asset);
        }
        // Validate all archive names before opening/truncating the output file.
        append_extra_files(members, files);
    }
    // The filesystem::path overload preserves the native wide path on Windows.
    // Passing output.string() here would incorrectly use the active ANSI codepage.
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    if (!stream) throw Error("Cannot open output file: " + path);
    if (extension == ".json") {
        stream << project.dump(2) << '\n';
    } else {
        write_zip(stream, members);
    }
    stream.flush();
    if (!stream) throw Error("Failed to write project: " + path);
}
} // namespace scratch
