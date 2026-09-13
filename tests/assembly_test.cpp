#include "scratch/assembly.hpp"
#include "scratch/platform.hpp"
#include <cstdint>
#include <filesystem>
#include <iostream>

using namespace scratch;
static void require(bool condition, const char* message) { if (!condition) throw Error(message); }
static Json integer(unsigned bits) { return Json{{"kind", "int"}, {"bits", bits}}; }
static Json void_type() { return Json{{"kind", "void"}, {"bits", 0}}; }
static Bytes constant(unsigned bits, std::int64_t number) {
    auto value = static_cast<std::uint64_t>(number);
    Bytes result;
    for (unsigned i = 0; i < (bits + 7) / 8; ++i) result.push_back((value >> (8 * i)) & 255u);
    if (bits % 8) result.back() = result.back().get<unsigned>() & ((1u << (bits % 8)) - 1);
    return result;
}
static NumericResult assemble(Project& project, const std::string& text, const std::string& constraints,
                              const Json& result, const std::vector<Json>& types = {},
                              const std::vector<Bytes>& operands = {}, bool effects = false) {
    return emit_assembly(Json{{"template", text}, {"constraints", constraints}, {"side_effects", effects}},
                         result, types, operands, project);
}
static void reject(const std::string& text, const std::string& constraints, const Json& result,
                    const std::vector<Json>& types = {}, const std::vector<Bytes>& operands = {}, bool effects = false) {
    Project project;
    try { (void)assemble(project, text, constraints, result, types, operands, effects); }
    catch (const Error&) { return; }
    throw Error("Invalid assembly was accepted: " + text);
}
int main(int argc, char** argv) {
    try {
        const auto args = platform::arguments(argc, argv);
        validate_assembly_template(" \n\t");
        validate_assembly_template("operator_add");
        validate_assembly_template("operator_add NUM1=$123 NUM2=$999");
        validate_assembly_template("pen_penDown; control_repeat TIMES=2 SUBSTACK { pen_penUp; }");
        validate_assembly_template("control_stop STOP_OPTION=\"all\"");
        reject("control_stop STOP_OPTION=\"all\"", "", void_type()); // side effect required
        reject("control_stop STOP_OPTION=\"other scripts in sprite\"", "", void_type(), {}, {}, true);
        for (const auto& text : {"pen_penDown; mov eax, ebx", "pen_penDown; control_repeat TIMES=1 SUBSTACK { mov eax, ebx; }",
                                 "operator_add NUM1=1", "$0=pen_penDown", "control_if CONDITION=true", "pen_penDown trailing"}) {
            bool failed = false;
            try { validate_assembly_template(text); } catch (const Error&) { failed = true; }
            require(failed, "Whole-template validation accepted invalid or embedded machine assembly");
        }
        Project project;
        project.lists["flags"] = Json::array({1, 0});
        Script main;
        Json expected = Json::object();
        const auto test = [&](const std::string& name, std::uint64_t expected_value, const std::string& text,
                              const std::string& constraints, const Json& type,
                              const std::vector<Json>& types = {}, const std::vector<Bytes>& operands = {},
                              bool effects = false) {
            auto result = assemble(project, text, constraints, type, types, operands, effects);
            require(result.bytes.size() == (type.at("bits").get<unsigned>() + 7) / 8, "Assembly result width mismatch");
            extend(main, result.code);
            Expr value = 0;
            for (std::size_t i = result.bytes.size(); i-- > 0;) value = add(mul(std::move(value), 256), result.bytes[i]);
            main.push_back(set(name, value));
            expected[name] = expected_value;
        };
        test("case_signed", 4294967291u, "operator_add NUM1=$1 NUM2=$2", "=r,r,r", integer(32),
             {integer(32), integer(32)}, {constant(32, -7), constant(32, 2)});
        test("case_min", 2147483647, "operator_add", "=r,r,r", integer(32),
             {integer(32), integer(32)}, {constant(32, -2147483648ll), constant(32, -1)});
        test("case_i8", 127, "operator_add", "=r,r,r", integer(8),
             {integer(8), integer(8)}, {constant(8, -128), constant(8, -1)});
        test("case_i1_input", 1, "operator_add NUM1=$1 NUM2=0", "=r,r", integer(32),
             {integer(1)}, {constant(1, 1)});
        test("case_boolean", 0, "operator_not", "=r,r", integer(1), {integer(1)}, {constant(1, 1)});
        test("case_compare", 1, "operator_lt", "=r,r,r", integer(1),
             {integer(32), integer(32)}, {constant(32, -7), constant(32, 2)});
        test("case_fraction", 4294967294u, "operator_divide NUM1=-5 NUM2=2", "=r", integer(32));
        test("case_sequence", 4294967286u,
             "$0 = operator_add NUM1=$1 NUM2=$2; $0 = operator_multiply NUM1=$0 NUM2=2;", "=&r,r,r", integer(32),
             {integer(32), integer(32)}, {constant(32, -7), constant(32, 2)});
        test("case_variable_loop", 3,
             "data_setvariableto VARIABLE=\"counter\" VALUE=-3; "
             "control_repeat TIMES=3 SUBSTACK { data_changevariableby VARIABLE=\"counter\" VALUE=2; }; "
             "$0 = data_variable VARIABLE=\"counter\"", "=r,~{memory}", integer(32), {}, {}, true);
        test("case_list_condition", 4294967289u,
             "control_if_else CONDITION=(data_itemoflist LIST=\"flags\" INDEX=1) "
             "SUBSTACK={ $0=operator_add NUM1=-7 NUM2=0; } "
             "SUBSTACK2={ $0=operator_add NUM1=7 NUM2=0; }", "=r", integer(32));
        test("case_repeat_until", 4,
             "$0=operator_add NUM1=0 NUM2=0; control_repeat_until "
             "CONDITION=(operator_equals OPERAND1=$0 OPERAND2=4) "
             "SUBSTACK { $0=operator_add NUM1=$0 NUM2=1; }", "=r", integer(32));
        test("case_mathop", 7, "operator_mathop OPERATOR=\"abs\" NUM=-7", "=r", integer(32));
        test("case_string", 5, "operator_length STRING=\"a;=$0\"", "=r", integer(32));
        test("case_infinity", 0, "operator_divide NUM1=1 NUM2=0", "=r", integer(32));
        test("case_unknown_number", 0, "operator_join STRING1=\"abc\" STRING2=\"def\"", "=r", integer(32));
        test("case_clang_clobbers", 4294967291u, "operator_add NUM1=$1 NUM2=$2",
             "=r,r,r,~{memory},~{dirflag},~{fpsr},~{flags}", integer(32),
             {integer(32), integer(32)}, {constant(32, -7), constant(32, 2)});
        auto snapshot = assemble(project,
            "data_setvariableto VARIABLE=\"source\" VALUE=99; $0=operator_add NUM1=$1 NUM2=0",
            "=r,r", integer(8), {integer(8)}, {Bytes{var("source")}}, true);
        main.push_back(set("source", 255));
        extend(main, snapshot.code);
        main.push_back(set("case_snapshot", snapshot.bytes.at(0)));
        expected["case_snapshot"] = 255;
        const auto barrier = assemble(project, " \n\t", "~{memory}", void_type(), {}, {}, true);
        require(barrier.code.empty() && barrier.bytes.empty(), "Empty memory barrier must emit no blocks");
        const auto clang_barrier = assemble(project, "", "~{memory},~{dirflag},~{fpsr},~{flags},~{cc}", void_type(), {}, {}, true);
        require(clang_barrier.code.empty() && clang_barrier.bytes.empty(), "Clang status clobbers must not emit target CPU operations");
        main.push_back(set("__status", "done"));
        project.procedure("main", {}, main);
        project.green_flag({call("main", {})});
        require(project.build()["targets"][1]["lists"].size() == 1, "Unexpected assembly list declarations");

        reject("mov eax, ebx", "", void_type());
        reject("event_whenflagclicked", "", void_type());
        reject("event_broadcast", "", void_type());
        reject("control_create_clone_of", "", void_type());
        reject("pen_penDown EXTRA=1", "", void_type(), {}, {}, true);
        reject("pen_penDown trailing", "", void_type(), {}, {}, true);
        reject("operator_add NUM1=1", "=r", integer(32));
        reject("operator_add NUM1=1 NUM1=2 NUM2=3", "=r", integer(32));
        reject("operator_add NUM1=$0 NUM2=1", "=r", integer(32));
        reject("operator_add NUM1=$2 NUM2=1", "=r,r", integer(32), {integer(32)}, {constant(32, 1)});
        reject("$1=operator_add NUM1=1 NUM2=2", "=r", integer(32));
        reject("$0=pen_penDown", "=r", integer(32), {}, {}, true);
        reject("$0=operator_add NUM1=1 NUM2=2", "", void_type());
        reject("control_if CONDITION=true SUBSTACK { $0=operator_add NUM1=1 NUM2=2; }", "=r", integer(32));
        reject("control_repeat TIMES=1 SUBSTACK { $0=operator_add NUM1=1 NUM2=2; }", "=r", integer(32));
        reject("control_if_else CONDITION=true SUBSTACK {}", "", void_type());
        reject("control_if CONDITION=true SUBSTACK {", "", void_type());
        reject("operator_add NUM1=(pen_penDown) NUM2=1", "=r", integer(32), {}, {}, true);
        reject("operator_add NUM1=1 NUM2=2 junk", "=r", integer(32));
        reject("operator_add NUM1=1 NUM2=2", "={eax}", integer(32));
        reject("operator_add NUM1=1 NUM2=2", "=r,~{rax}", integer(32));
        reject("operator_add NUM1=1 NUM2=2", "=r,~{x0}", integer(32));
        reject("operator_add NUM1=1 NUM2=2", "=r,~{flags},~{flags}", integer(32));
        reject("operator_add NUM1=1 NUM2=2", "=r,~{flags},r", integer(32));
        reject("operator_add NUM1=1 NUM2=2", "=r,", integer(32));
        reject("operator_add NUM1=1 NUM2=2", "=r", integer(64));
        reject("operator_add NUM1=1 NUM2=2", "=r", Json{{"kind", "float"}, {"bits", 32}});
        reject("", "r", void_type(), {integer(32)}, {constant(32, 1)});
        reject("", "~{memory},~{memory}", void_type());
        reject("pen_penDown", "", void_type());
        reject("operator_random FROM=1 TO=2", "=r", integer(32));
        reject("operator_mathop OPERATOR=\"bad\" NUM=1", "=r", integer(32));
        reject("data_variable VARIABLE=$1", "=r,r", integer(32), {integer(32)}, {constant(32, 1)});
        reject("data_variable VARIABLE=\"__scl_asm0_value\"", "=r", integer(32));

        Project pen;
        auto drawing = assemble(pen,
            "pen_clear; pen_setPenColorToColor COLOR=\"#123456\"; "
            "pen_setPenColorParamTo COLOR_PARAM=\"transparency\" VALUE=0; "
            "pen_setPenSizeTo SIZE=3; pen_penDown; motion_gotoxy X=-7 Y=4; pen_penUp",
            "", void_type(), {}, {}, true);
        drawing.code.push_back(set("__status", "done"));
        pen.procedure("main", {}, drawing.code);
        pen.green_flag({call("main", {})});
        require(pen.build()["extensions"] == Json::array({"pen"}), "Assembly pen extension was not registered");
        if (args.size() > 1) {
            const auto directory = std::filesystem::u8path(args[1]);
            std::filesystem::create_directories(directory);
            project.files["assembly-test-expected.json"] = expected.dump(2);
            project.save((directory / "assembly-smoke.sb3").u8string());
            pen.save((directory / "assembly-pen.sb3").u8string());
        }
        std::cout << "assembly_test: parser, constraints, bridges and diagnostics passed; VM expected=" << expected.dump() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "assembly_test: " << error.what() << '\n'; return 1;
    }
}
