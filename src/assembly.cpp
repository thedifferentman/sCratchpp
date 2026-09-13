#include "scratch/assembly.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace scratch {
namespace {
[[noreturn]] void fail(const std::string& message) { throw Error("Scratch inline assembly: " + message); }

enum class Shape { Command, Reporter, Boolean };
struct Spec {
    Shape shape;
    std::vector<std::string> inputs;
    std::vector<std::string> fields;
    std::vector<std::string> branches;
    bool writes = false;
};
const std::map<std::string, Spec>& opcodes() {
    static const auto result = [] {
        std::map<std::string, Spec> map;
        const auto add = [&](const char* name, Shape shape, std::vector<std::string> inputs = {},
                             std::vector<std::string> fields = {}, std::vector<std::string> branches = {},
                             bool writes = false) {
            map.emplace(name, Spec{shape, std::move(inputs), std::move(fields), std::move(branches), writes});
        };
        for (const auto name : {"operator_add", "operator_subtract", "operator_multiply", "operator_divide", "operator_mod"})
            add(name, Shape::Reporter, {"NUM1", "NUM2"});
        for (const auto name : {"operator_equals", "operator_lt", "operator_gt", "operator_and", "operator_or"})
            add(name, Shape::Boolean, {"OPERAND1", "OPERAND2"});
        add("operator_not", Shape::Boolean, {"OPERAND"});
        add("operator_random", Shape::Reporter, {"FROM", "TO"}, {}, {}, true);
        add("operator_join", Shape::Reporter, {"STRING1", "STRING2"});
        add("operator_letter_of", Shape::Reporter, {"LETTER", "STRING"});
        add("operator_length", Shape::Reporter, {"STRING"});
        add("operator_contains", Shape::Boolean, {"STRING1", "STRING2"});
        add("operator_round", Shape::Reporter, {"NUM"});
        add("operator_mathop", Shape::Reporter, {"NUM"}, {"OPERATOR"});
        add("data_variable", Shape::Reporter, {}, {"VARIABLE"});
        add("data_listcontents", Shape::Reporter, {}, {"LIST"});
        add("data_itemoflist", Shape::Reporter, {"INDEX"}, {"LIST"});
        add("data_itemnumoflist", Shape::Reporter, {"ITEM"}, {"LIST"});
        add("data_lengthoflist", Shape::Reporter, {}, {"LIST"});
        add("data_listcontainsitem", Shape::Boolean, {"ITEM"}, {"LIST"});
        add("data_setvariableto", Shape::Command, {"VALUE"}, {"VARIABLE"}, {}, true);
        add("data_changevariableby", Shape::Command, {"VALUE"}, {"VARIABLE"}, {}, true);
        for (const auto name : {"data_showvariable", "data_hidevariable"})
            add(name, Shape::Command, {}, {"VARIABLE"}, {}, true);
        add("data_addtolist", Shape::Command, {"ITEM"}, {"LIST"}, {}, true);
        add("data_deleteoflist", Shape::Command, {"INDEX"}, {"LIST"}, {}, true);
        add("data_insertatlist", Shape::Command, {"INDEX", "ITEM"}, {"LIST"}, {}, true);
        add("data_replaceitemoflist", Shape::Command, {"INDEX", "ITEM"}, {"LIST"}, {}, true);
        for (const auto name : {"data_deletealloflist", "data_showlist", "data_hidelist"})
            add(name, Shape::Command, {}, {"LIST"}, {}, true);
        add("motion_movesteps", Shape::Command, {"STEPS"}, {}, {}, true);
        add("motion_turnright", Shape::Command, {"DEGREES"}, {}, {}, true);
        add("motion_turnleft", Shape::Command, {"DEGREES"}, {}, {}, true);
        add("motion_gotoxy", Shape::Command, {"X", "Y"}, {}, {}, true);
        add("motion_glidesecstoxy", Shape::Command, {"SECS", "X", "Y"}, {}, {}, true);
        add("motion_pointindirection", Shape::Command, {"DIRECTION"}, {}, {}, true);
        add("motion_changexby", Shape::Command, {"DX"}, {}, {}, true);
        add("motion_setx", Shape::Command, {"X"}, {}, {}, true);
        add("motion_changeyby", Shape::Command, {"DY"}, {}, {}, true);
        add("motion_sety", Shape::Command, {"Y"}, {}, {}, true);
        add("motion_ifonedgebounce", Shape::Command, {}, {}, {}, true);
        add("motion_setrotationstyle", Shape::Command, {}, {"STYLE"}, {}, true);
        for (const auto name : {"motion_xposition", "motion_yposition", "motion_direction"}) add(name, Shape::Reporter);
        for (const auto name : {"looks_say", "looks_think"}) add(name, Shape::Command, {"MESSAGE"}, {}, {}, true);
        for (const auto name : {"looks_sayforsecs", "looks_thinkforsecs"})
            add(name, Shape::Command, {"MESSAGE", "SECS"}, {}, {}, true);
        for (const auto name : {"looks_show", "looks_hide", "looks_cleargraphiceffects"})
            add(name, Shape::Command, {}, {}, {}, true);
        add("looks_changesizeby", Shape::Command, {"CHANGE"}, {}, {}, true);
        add("looks_setsizeto", Shape::Command, {"SIZE"}, {}, {}, true);
        add("looks_changeeffectby", Shape::Command, {"CHANGE"}, {"EFFECT"}, {}, true);
        add("looks_seteffectto", Shape::Command, {"VALUE"}, {"EFFECT"}, {}, true);
        add("looks_size", Shape::Reporter);
        for (const auto name : {"sensing_timer", "sensing_dayssince2000", "sensing_mousex", "sensing_mousey",
                                "sensing_loudness", "sensing_answer", "sensing_username"}) add(name, Shape::Reporter);
        add("sensing_mousedown", Shape::Boolean);
        add("sensing_keypressed", Shape::Boolean, {"KEY_OPTION"});
        add("sensing_current", Shape::Reporter, {}, {"CURRENTMENU"});
        add("sensing_resettimer", Shape::Command, {}, {}, {}, true);
        add("sensing_askandwait", Shape::Command, {"QUESTION"}, {}, {}, true);
        add("control_if", Shape::Command, {"CONDITION"}, {}, {"SUBSTACK"});
        add("control_if_else", Shape::Command, {"CONDITION"}, {}, {"SUBSTACK", "SUBSTACK2"});
        add("control_repeat", Shape::Command, {"TIMES"}, {}, {"SUBSTACK"});
        add("control_repeat_until", Shape::Command, {"CONDITION"}, {}, {"SUBSTACK"});
        add("control_wait", Shape::Command, {"DURATION"}, {}, {}, true);
        add("control_wait_until", Shape::Command, {"CONDITION"}, {}, {}, true);
        add("control_stop", Shape::Command, {}, {"STOP_OPTION"}, {}, true);
        for (const auto name : {"pen_clear", "pen_stamp", "pen_penDown", "pen_penUp"})
            add(name, Shape::Command, {}, {}, {}, true);
        add("pen_setPenColorToColor", Shape::Command, {"COLOR"}, {}, {}, true);
        for (const auto name : {"pen_changePenColorParamBy", "pen_setPenColorParamTo"})
            add(name, Shape::Command, {"COLOR_PARAM", "VALUE"}, {}, {}, true);
        for (const auto name : {"pen_changePenSizeBy", "pen_setPenSizeTo"})
            add(name, Shape::Command, {"SIZE"}, {}, {}, true);
        for (const auto name : {"pen_changePenHueBy", "pen_setPenHueToNumber"})
            add(name, Shape::Command, {"HUE"}, {}, {}, true);
        for (const auto name : {"pen_changePenShadeBy", "pen_setPenShadeToNumber"})
            add(name, Shape::Command, {"SHADE"}, {}, {}, true);
        return map;
    }();
    return result;
}
const Spec& specification(const std::string& opcode) {
    const auto found = opcodes().find(opcode);
    if (found == opcodes().end())
        fail("unsupported opcode '" + opcode + "'; hats, broadcasts, clones, machine assembly and unlisted opcodes are rejected");
    return found->second;
}
bool contains(const std::vector<std::string>& names, const std::string& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}
std::string trim(std::string text) {
    const auto white = [](unsigned char c) { return std::isspace(c) != 0; };
    text.erase(text.begin(), std::find_if_not(text.begin(), text.end(), white));
    text.erase(std::find_if_not(text.rbegin(), text.rend(), white).base(), text.end());
    return text;
}

enum class TokenKind { Word, Number, String, Operand, Punctuation, End };
struct Token { TokenKind kind; std::string text; std::size_t offset; };
class Parser {
public:
    explicit Parser(std::string text) : text_(std::move(text)) { advance(); }
    Json parse() {
        auto result = sequence(false, 0);
        if (token_.kind != TokenKind::End) syntax("unexpected trailing token");
        return result;
    }
private:
    std::string text_;
    std::size_t position_ = 0;
    Token token_{TokenKind::End, "", 0};
    [[noreturn]] void syntax(const std::string& message) const {
        fail(message + " at template byte " + std::to_string(token_.offset));
    }
    bool is(const char* value) const { return token_.text == value && token_.kind != TokenKind::String; }
    void advance() {
        while (position_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[position_]))) ++position_;
        token_ = {TokenKind::End, "", position_};
        if (position_ == text_.size()) return;
        const auto start = position_;
        const auto c = static_cast<unsigned char>(text_[position_++]);
        if (c == '"') {
            bool escape = false;
            while (position_ < text_.size()) {
                const char next = text_[position_++];
                if (!escape && next == '"') {
                    token_ = {TokenKind::String, text_.substr(start, position_ - start), start}; return;
                }
                if (!escape && next == '\\') escape = true;
                else escape = false;
            }
            syntax("unterminated quoted string");
        }
        if (c == '$') {
            while (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_]))) ++position_;
            if (position_ == start + 1) syntax("operand must be $ followed by an index");
            token_ = {TokenKind::Operand, text_.substr(start + 1, position_ - start - 1), start}; return;
        }
        if (std::isalpha(c) || c == '_') {
            while (position_ < text_.size()) {
                const auto next = static_cast<unsigned char>(text_[position_]);
                if (!std::isalnum(next) && next != '_') break;
                ++position_;
            }
            token_ = {TokenKind::Word, text_.substr(start, position_ - start), start}; return;
        }
        if (std::isdigit(c) || c == '-') {
            while (position_ < text_.size()) {
                const char next = text_[position_];
                if (!std::isdigit(static_cast<unsigned char>(next)) && next != '.' && next != 'e' && next != 'E' &&
                    next != '+' && next != '-') break;
                ++position_;
            }
            token_ = {TokenKind::Number, text_.substr(start, position_ - start), start}; return;
        }
        if (std::string("=;{}()").find(static_cast<char>(c)) == std::string::npos) syntax("unexpected character");
        token_ = {TokenKind::Punctuation, std::string(1, static_cast<char>(c)), start};
    }
    void expect(const char* value) {
        if (!is(value)) syntax(std::string("expected '") + value + "'");
        advance();
    }
    Json sequence(bool nested, unsigned depth) {
        if (depth > 128) syntax("template nesting exceeds 128 levels");
        Json result = Json::array();
        while (token_.kind != TokenKind::End && !is("}")) {
            if (is(";")) syntax("empty statements are not accepted");
            result.push_back(operation(false, depth));
            if (is(";")) advance();
            else if (token_.kind != TokenKind::End && !is("}")) syntax("expected ';' between statements");
        }
        if (nested && token_.kind == TokenKind::End) syntax("unterminated SUBSTACK");
        return result;
    }
    Json operation(bool nested_reporter, unsigned depth) {
        if (depth > 128) syntax("template nesting exceeds 128 levels");
        bool assigns = false;
        if (token_.kind == TokenKind::Operand) {
            if (nested_reporter) syntax("assignments cannot appear inside reporter inputs");
            if (token_.text != "0") syntax("only the output $0 can be assigned");
            advance(); expect("="); assigns = true;
        }
        if (token_.kind != TokenKind::Word) syntax("expected official Scratch opcode");
        const auto opcode = token_.text;
        const auto& spec = specification(opcode);
        if (nested_reporter && spec.shape == Shape::Command) syntax("command cannot be used as a reporter input");
        advance();
        Json result{{"opcode", opcode}, {"assign", assigns}, {"bindings", Json::object()}, {"branches", Json::object()}};
        while (token_.kind == TokenKind::Word) {
            const auto name = token_.text;
            if (!contains(spec.inputs, name) && !contains(spec.fields, name) && !contains(spec.branches, name))
                syntax("unknown binding '" + name + "' for " + opcode);
            if (result["bindings"].contains(name) || result["branches"].contains(name)) syntax("duplicate binding '" + name + "'");
            advance();
            if (is("=")) advance();
            else if (!contains(spec.branches, name)) syntax("expected '=' after binding name");
            if (contains(spec.branches, name)) {
                expect("{"); result["branches"][name] = sequence(true, depth + 1); expect("}");
            } else result["bindings"][name] = value(depth + 1);
        }
        return result;
    }
    Json value(unsigned depth) {
        if (token_.kind == TokenKind::Operand) {
            unsigned long long index = 0;
            try { index = std::stoull(token_.text); } catch (const std::exception&) { syntax("operand index is too large"); }
            if (index > std::numeric_limits<unsigned>::max()) syntax("operand index is too large");
            advance(); return Json{{"kind", "operand"}, {"index", index}};
        }
        if (token_.kind == TokenKind::String || token_.kind == TokenKind::Number) {
            Json decoded;
            try { decoded = Json::parse(token_.text); } catch (const std::exception&) { syntax("invalid JSON string or number literal"); }
            if (decoded.is_number_float() && !std::isfinite(decoded.get<double>())) syntax("number literal must be finite");
            advance(); return Json{{"kind", "literal"}, {"value", decoded}};
        }
        if (is("true") || is("false")) {
            const auto b = is("true"); advance(); return Json{{"kind", "literal"}, {"value", b ? 1 : 0}};
        }
        if (is("(")) {
            advance(); auto nested = operation(true, depth); expect(")");
            return Json{{"kind", "reporter"}, {"node", nested}};
        }
        syntax("expected $operand, JSON literal or parenthesized reporter");
    }
};

void validate_structure(const Json& program, bool top_level);
void validate_operation(const Json& operation, bool implicit_inputs, bool nested_reporter) {
    const auto opcode = operation.at("opcode").get<std::string>();
    const auto& spec = specification(opcode);
    if (operation.at("assign").get<bool>() && spec.shape == Shape::Command)
        fail("command " + opcode + " cannot be assigned to an output");
    if (!operation.at("assign").get<bool>() && spec.shape != Shape::Command && !implicit_inputs && !nested_reporter)
        fail("standalone reporter requires '$0 =' in a command sequence");
    const auto& bindings = operation.at("bindings");
    const auto& branches = operation.at("branches");
    const bool positional = implicit_inputs && bindings.empty() && branches.empty() &&
                            spec.fields.empty() && spec.branches.empty();
    for (const auto& name : spec.inputs)
        if (!bindings.contains(name) && !positional) fail("missing input '" + name + "' for " + opcode);
    for (const auto& name : spec.fields) {
        if (!bindings.contains(name)) fail("missing field '" + name + "' for " + opcode);
        const auto& source = bindings[name];
        if (source.at("kind") != "literal" || !source.at("value").is_string())
            fail("field " + name + " requires a quoted literal string");
    }
    if (opcode == "control_stop" && bindings.at("STOP_OPTION").at("value") != "all")
        fail("control_stop currently supports only STOP_OPTION=\"all\"");
    for (const auto& name : spec.branches) {
        if (!branches.contains(name)) fail("missing " + name + " for " + opcode);
        validate_structure(branches[name], false);
    }
    for (const auto& binding : bindings.items()) {
        if (binding.value().at("kind") == "reporter")
            validate_operation(binding.value().at("node"), false, true);
    }
}
void validate_structure(const Json& program, bool top_level) {
    for (const auto& operation : program) validate_operation(operation, top_level && program.size() == 1, false);
}

unsigned integer_bits(const Json& type, const std::string& context) {
    if (type.value("kind", "") != "int") fail(context + " requires an integer r operand; float, pointer and aggregate bridges are not implemented");
    const auto bits = type.value("bits", 0u);
    if (bits == 0 || bits > 32) fail(context + " supports only i1 through i32");
    return bits;
}
void check_constraints(const std::string& text, bool output, std::size_t inputs) {
    std::vector<std::string> constraints;
    std::size_t start = 0;
    std::set<std::string> clobbers;
    if (!trim(text).empty()) {
        while (start <= text.size()) {
            const auto comma = text.find(',', start);
            const auto part = trim(text.substr(start, comma == std::string::npos ? comma : comma - start));
            if (part.empty()) fail("empty operand constraint");
            if (part.rfind("~{", 0) == 0) {
                // Clang adds x86 state clobbers even for an otherwise target-
                // independent C asm template. Scratch has no corresponding CPU
                // flags/status registers; memory still retains its IR barrier role.
                static const std::set<std::string> supported = {
                    "~{memory}", "~{dirflag}", "~{fpsr}", "~{flags}", "~{cc}"};
                if (!supported.count(part)) fail("unsupported register/state clobber '" + part + "'");
                if (!clobbers.insert(part).second) fail("duplicate clobber '" + part + "'");
            } else {
                if (!clobbers.empty()) fail("clobbers must follow operand constraints");
                constraints.push_back(part);
            }
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    }
    if (constraints.size() != inputs + (output ? 1 : 0)) fail("constraint count does not match result and LLVM operands");
    if (output && constraints.front() != "=r" && constraints.front() != "=&r")
        fail("one direct =r (or =&r) result is required; tied, indirect and multiple outputs are unsupported");
    for (std::size_t i = output ? 1 : 0; i < constraints.size(); ++i)
        if (constraints[i] != "r") fail("unsupported input constraint '" + constraints[i] + "'; use r");
}

class Lowerer {
public:
    Script code;
    std::vector<Expr> inputs;
    bool has_output;
    unsigned output_bits;
    std::string output_name;
    Lowerer(Project& project, bool output, unsigned bits, bool side_effects)
        : has_output(output), output_bits(bits), project_(project), side_effects_(side_effects) {
        for (std::size_t index = 0;; ++index) {
            prefix_ = "__scl_asm" + std::to_string(index) + "_";
            if (!project_.variables.contains(prefix_ + "value")) break;
        }
        output_name = prefix_ + "value";
        project_.variables[output_name] = 0;
    }
    void bridge_inputs(const std::vector<Json>& types, const std::vector<Bytes>& operands) {
        for (std::size_t i = 0; i < types.size(); ++i) {
            const auto bits = integer_bits(types[i], "input " + std::to_string(i));
            const auto count = (bits + 7) / 8;
            if (operands[i].size() != count) fail("input byte count does not match its integer type");
            Expr value = 0;
            for (unsigned j = count; j-- > 0;) {
                Expr byte = operands[i][j];
                if (j + 1 == count && bits % 8) byte = mod(byte, 1u << (bits % 8));
                value = add(mul(std::move(value), 256), std::move(byte));
            }
            const auto name = prefix_ + "in" + std::to_string(i);
            code.push_back(set(name, std::move(value)));
            if (bits > 1) code.push_back(iff(lnot(lt(var(name), std::ldexp(1.0, bits - 1))),
                {set(name, sub(var(name), std::ldexp(1.0, bits)))}));
            inputs.push_back(var(name));
        }
    }
    void program(Json program) {
        if (program.size() == 1) {
            auto& operation = program[0];
            const auto& spec = specification(operation["opcode"].get<std::string>());
            if (operation["bindings"].empty() && operation["branches"].empty()) {
                if (!spec.fields.empty() || !spec.branches.empty()) fail("bare opcode needs explicit fields or SUBSTACK bindings");
                if (spec.inputs.size() != inputs.size()) fail("bare opcode input count differs from LLVM operand count");
                for (std::size_t i = 0; i < spec.inputs.size(); ++i)
                    operation["bindings"][spec.inputs[i]] = Json{{"kind", "operand"}, {"index", i + (has_output ? 1 : 0)}};
            }
            if (spec.shape != Shape::Command && has_output) operation["assign"] = true;
        }
        bool assigned = false;
        extend(code, sequence(program, assigned));
        if (has_output && !assigned) fail("output $0 is not assigned on every possible path");
    }
    Bytes bridge_output() {
        if (!has_output) return {};
        // Convert Scratch's universal result once. Non-numeric strings and NaN
        // follow Scratch's numeric conversion to zero; infinities also map to zero.
        code.push_back(set(output_name, mul(var(output_name), 1)));
        code.push_back(iff(lor(eq(var(output_name), "Infinity"), eq(var(output_name), "-Infinity")), {set(output_name, 0)}));
        code.push_back(iff(lt(var(output_name), 0),
            {set(output_name, expr("operator_mathop", Json{{"NUM", var(output_name)}}, Json{{"OPERATOR", Json::array({"ceiling", nullptr})}}))},
            {set(output_name, floor_(var(output_name)))}));
        code.push_back(set(output_name, mod(var(output_name), std::ldexp(1.0, output_bits))));
        Bytes bytes;
        for (unsigned i = 0; i < (output_bits + 7) / 8; ++i) {
            const auto name = prefix_ + "byte" + std::to_string(i);
            code.push_back(set(name, mod(floor_(scratch::div(var(output_name), std::ldexp(1.0, i * 8))), 256)));
            bytes.push_back(var(name));
        }
        return bytes;
    }
private:
    Project& project_;
    bool side_effects_;
    std::string prefix_;
    Expr value(const Json& source, bool assigned) {
        const auto kind = source.at("kind").get<std::string>();
        if (kind == "literal") return source.at("value");
        if (kind == "operand") {
            auto index = source.at("index").get<std::size_t>();
            if (has_output) {
                if (index == 0) {
                    if (!assigned) fail("output $0 is read before it is assigned");
                    return var(output_name);
                }
                --index;
            }
            if (index >= inputs.size()) fail("operand reference is outside the LLVM argument list");
            return inputs[index];
        }
        if (kind == "reporter") return block(source.at("node"), assigned, true);
        fail("invalid internal assembly value");
    }
    Json fields(const Json& operation, const Spec& spec) {
        Json result = Json::object();
        for (const auto& name : spec.fields) {
            const auto& bindings = operation.at("bindings");
            if (!bindings.contains(name)) fail("missing field '" + name + "' for " + operation.at("opcode").get<std::string>());
            const auto& source = bindings[name];
            if (source.at("kind") != "literal" || !source.at("value").is_string()) fail("field " + name + " requires a quoted literal string");
            auto text = source.at("value").get<std::string>();
            if (name == "VARIABLE" || name == "LIST") {
                if (text.empty() || text.find('\0') != std::string::npos) fail("variable/list name is empty or contains NUL");
                if (text.rfind("__scl_asm", 0) == 0) fail("__scl_asm is reserved for assembly bridge temporaries");
                auto& declarations = name == "VARIABLE" ? project_.variables : project_.lists;
                if (!declarations.contains(text)) declarations[text] = name == "VARIABLE" ? Json(0) : Json::array();
            } else {
                static const std::map<std::string, std::set<std::string>> enums = {
                    {"OPERATOR", {"abs", "floor", "ceiling", "sqrt", "sin", "cos", "tan", "asin", "acos", "atan", "ln", "log", "e ^", "10 ^"}},
                    {"STYLE", {"left-right", "don't rotate", "all around"}},
                    {"EFFECT", {"COLOR", "FISHEYE", "WHIRL", "PIXELATE", "MOSAIC", "BRIGHTNESS", "GHOST"}},
                    {"CURRENTMENU", {"YEAR", "MONTH", "DATE", "DAYOFWEEK", "HOUR", "MINUTE", "SECOND"}}};
                const auto options = enums.find(name);
                if (options != enums.end() && !options->second.count(text)) fail("invalid " + name + " field value '" + text + "'");
            }
            result[name] = Json::array({text, nullptr});
        }
        return result;
    }
    Expr block(const Json& operation, bool assigned, bool reporter) {
        const auto opcode = operation.at("opcode").get<std::string>();
        const auto& spec = specification(opcode);
        if (reporter && spec.shape == Shape::Command) fail("command cannot supply an input or output value");
        if (spec.writes && !side_effects_) fail(opcode + " requires asm sideeffect to preserve its observable effect");
        Json inputs_json = Json::object();
        for (const auto& name : spec.inputs) {
            const auto& bindings = operation.at("bindings");
            if (!bindings.contains(name)) fail("missing input '" + name + "' for " + opcode);
            auto input = value(bindings[name], assigned);
            if (name == "COLOR_PARAM" && input.is_string()) {
                const std::set<std::string> options{"color", "saturation", "brightness", "transparency"};
                if (!options.count(input.get<std::string>())) fail("invalid pen COLOR_PARAM");
                auto menu = expr("pen_menu_colorParam", Json::object(), Json{{"colorParam", Json::array({input, nullptr})}});
                menu["shadow"] = true; input = std::move(menu);
            }
            inputs_json[name] = std::move(input);
        }
        for (const auto& branch : spec.branches)
            if (!operation.at("branches").contains(branch)) fail("missing " + branch + " for " + opcode);
        return expr(opcode, inputs_json, fields(operation, spec));
    }
    Script sequence(const Json& operations, bool& assigned) {
        Script result;
        for (const auto& operation : operations) {
            const auto opcode = operation.at("opcode").get<std::string>();
            const auto& spec = specification(opcode);
            const bool assigns = operation.at("assign").get<bool>();
            if (assigns && !has_output) fail("void assembly has no output $0 to assign");
            if (spec.shape != Shape::Command && !assigns) fail("standalone reporter requires '$0 =' in a command sequence");
            auto ast = block(operation, assigned, assigns);
            if (assigns) {
                result.push_back(set(output_name, std::move(ast)));
                assigned = true;
            } else {
                const bool incoming = assigned;
                bool all_branches = !spec.branches.empty();
                for (const auto& name : spec.branches) {
                    bool branch_assigned = incoming;
                    ast["branches"][name] = sequence(operation.at("branches").at(name), branch_assigned);
                    all_branches = all_branches && branch_assigned;
                }
                if (opcode == "control_if_else") assigned = all_branches;
                // An if without else, repeat and repeat-until may execute no body.
                result.push_back(std::move(ast));
            }
        }
        return result;
    }
};
} // namespace

void validate_assembly_template(const std::string& text) {
    if (trim(text).empty()) return;
    validate_structure(Parser(text).parse(), true);
}

NumericResult emit_assembly(const Json& asm_info, const Json& return_type,
                            const std::vector<Json>& arg_types, const std::vector<Bytes>& operands,
                            Project& project) {
    if (!asm_info.is_object() || !asm_info.contains("template") || !asm_info.at("template").is_string() ||
        !asm_info.contains("constraints") || !asm_info.at("constraints").is_string())
        fail("expected template and constraints strings");
    if (arg_types.size() != operands.size()) fail("LLVM argument types and byte operands differ in count");
    const auto text = asm_info.at("template").get<std::string>();
    const auto constraints = asm_info.at("constraints").get<std::string>();
    const bool output = return_type.value("kind", "") != "void";
    const auto bits = output ? integer_bits(return_type, "result") : 0;
    check_constraints(constraints, output, operands.size());
    if (trim(text).empty()) {
        if (output || !operands.empty()) fail("empty assembly must have no result or operands");
        return {};
    }
    auto program = Parser(text).parse();
    validate_structure(program, true);
    Lowerer lowerer(project, output, bits, asm_info.value("side_effects", false));
    lowerer.bridge_inputs(arg_types, operands);
    lowerer.program(std::move(program));
    auto bytes = lowerer.bridge_output();
    return NumericResult{std::move(lowerer.code), std::move(bytes)};
}
} // namespace scratch
