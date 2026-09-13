#include "scratch/frontend.hpp"
#include "scratch/float_lower.hpp"
#include "scratch/platform.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace scratch;

static void require(bool condition, const char* message) {
    if (!condition) throw Error(message);
}

int main(int argc, char** argv) {
    try {
        const auto args = platform::arguments(argc, argv);
        if (args.size() != 2) throw Error("usage: frontend-debug-test <output-directory>");
        const auto directory = std::filesystem::u8path(args[1]);
        std::filesystem::create_directories(directory);
        const auto path = directory / "debug-source.ll";
        {
            std::ofstream output(path, std::ios::binary);
            output << R"IR(
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
define i32 @main(i32 %input) !dbg !4 {
entry:
  %storage = alloca i32, align 4
    #dbg_value(i32 %input, !7, !DIExpression(), !10)
    #dbg_declare(ptr %storage, !8, !DIExpression(), !10)
  store i32 %input, ptr %storage, !dbg !10
  %value = load i32, ptr %storage, !dbg !11
    #dbg_value(i32 %value, !9, !DIExpression(), !11)
  %sum = add i32 %value, 3, !dbg !12
    #dbg_value(i32 poison, !9, !DIExpression(), !12)
    #dbg_value(i32 %sum, !15, !DIExpression(DW_OP_plus_uconst, 1), !12)
  ret i32 %sum, !dbg !13
}
define float @compute(float %a, float %b) !dbg !17 {
entry:
    #dbg_value(float %a, !18, !DIExpression(), !20)
  %sum = fadd float %a, %b, !dbg !20
  ret float %sum, !dbg !21
}
@global = global i32 0
declare i1 @llvm.is.constant.i32(i32)
declare i1 @llvm.is.constant.p0(ptr)
declare i64 @llvm.objectsize.i64.p0(ptr, i1 immarg, i1 immarg, i1 immarg)
define i32 @constant_queries(i32 %input, ptr %unknown_object) noinline optnone {
entry:
  %buffer = alloca [16 x i8], align 1
  %known = call i1 @llvm.is.constant.i32(i32 7)
  %unknown = call i1 @llvm.is.constant.i32(i32 %input)
  %address = call i1 @llvm.is.constant.p0(ptr @global)
  %size = call i64 @llvm.objectsize.i64.p0(ptr %buffer, i1 false, i1 true, i1 false)
  %size32 = trunc i64 %size to i32
  %a = select i1 %known, i32 7, i32 1000
  %b = select i1 %unknown, i32 1000, i32 0
  %c = select i1 %address, i32 1000, i32 0
  %ab = add i32 %a, %b
  %abc = add i32 %ab, %c
  %result = add i32 %abc, %size32
  ret i32 %result
}
!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}
!0 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: !1, producer: "scratch-debug-test", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug)
!1 = !DIFile(filename: "example.cpp", directory: "/debug/project")
!2 = !{i32 2, !"Dwarf Version", i32 5}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = distinct !DISubprogram(name: "main", scope: !1, file: !1, line: 2, type: !5, scopeLine: 2, spFlags: DISPFlagDefinition, unit: !0)
!5 = !DISubroutineType(types: !6)
!6 = !{!14, !14}
!7 = !DILocalVariable(name: "input", arg: 1, scope: !4, file: !1, line: 2, type: !14)
!8 = !DILocalVariable(name: "local", scope: !4, file: !1, line: 3, type: !16)
!9 = !DILocalVariable(name: "copy", scope: !4, file: !1, line: 4, type: !14)
!10 = !DILocation(line: 3, column: 7, scope: !4)
!11 = !DILocation(line: 4, column: 9, scope: !4)
!12 = !DILocation(line: 5, column: 10, scope: !4)
!13 = !DILocation(line: 6, column: 3, scope: !4)
!14 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!15 = !DILocalVariable(name: "complex", scope: !4, file: !1, line: 5, type: !14)
!16 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !14)
!17 = distinct !DISubprogram(name: "compute", scope: !1, file: !1, line: 8, type: !23, scopeLine: 8, spFlags: DISPFlagDefinition, unit: !0)
!18 = !DILocalVariable(name: "a", arg: 1, scope: !17, file: !1, line: 8, type: !22)
!20 = !DILocation(line: 9, column: 4, scope: !17)
!21 = !DILocation(line: 10, column: 3, scope: !17)
!22 = !DIBasicType(name: "float", size: 32, encoding: DW_ATE_float)
!23 = !DISubroutineType(types: !24)
!24 = !{!22, !22, !22}
)IR";
        }
        FrontendOptions options;
        const auto plain = read_modules({path.u8string()}, options);
        require(!plain.contains("debug_info"), "release module acquired debug data");
        require(!plain.at("functions")[0].contains("debug"), "release function acquired debug data");
        options.debug_info = true;
        const auto module = read_modules({path.u8string()}, options);
        std::ofstream(directory / "debug-source.json", std::ios::binary) << module.dump(2);
        require(module.contains("debug_info"), "debug mode marker missing");
        const auto& function = module.at("functions")[0];
        const auto& queries = module.at("functions")[2];
        require(queries.at("attrs").contains("optnone"), "mandatory intrinsic lowering removed optnone permanently");
        const auto& query_instructions = queries.at("blocks")[0].at("instructions");
        for (const auto& inst : query_instructions)
            require(inst.at("op") != "call", "constant query intrinsic survived in optnone function");
        const auto& query_result = query_instructions.back().at("operands")[0];
        require(query_result.at("kind") == "bytes" && query_result.at("bytes")[0] == 23,
            "constant query lowering confused constants, globals, or known object sizes");
        require(function.at("debug").at("name") == "main", "source function name missing");
        require(function.at("debug").at("file") == "/debug/project/example.cpp", "source file path corrupted");
        const auto& instructions = function.at("blocks")[0].at("instructions");
        require(!instructions[0].contains("debug"), "missing source location was invented");
        require(instructions[1].at("debug").at("line") == 3 && instructions[1].at("debug").at("column") == 7,
            "instruction source location lost");
        const auto& variables = function.at("debug_variables");
        require(variables.size() == 4, "debug variable records lost");
        require(variables[0].at("name") == "input" && variables[0].at("parameter") == 1,
            "parameter name or index lost");
        require(variables[0].at("type").at("signed") == true && variables[0].at("type").at("bits") == 32,
            "signed integer source type lost");
        require(variables[0].at("locations")[0].at("kind") == "value" &&
            variables[0].at("locations")[0].at("operand").at("id") == function.at("args")[0].at("id"),
            "parameter SSA location incorrect");
        require(variables[1].at("locations")[0].at("kind") == "address" &&
            variables[1].at("locations")[0].at("operand").at("id") == instructions[0].at("id"),
            "local address location incorrect");
        require(variables[1].at("type").at("kind") == "int" && variables[1].at("type").at("bytes") == 4,
            "derived const type did not unwrap");
        require(variables[2].at("locations").size() == 2 &&
            variables[2].at("locations")[1].at("kind") == "unavailable", "poison debug value not marked unavailable");
        require(variables[3].at("locations")[0].at("kind") == "unavailable", "complex expression fabricated a value");
        require(variables[2].at("locations")[0].at("before") == instructions[3].at("id"),
            "debug record instruction boundary incorrect");
        // Source metadata must not perturb normal instruction IDs/layout.
        for (size_t i = 0; i < instructions.size(); ++i)
            require(instructions[i].at("id") == plain.at("functions")[0].at("blocks")[0].at("instructions")[i].at("id"),
                "debug extraction changed instruction IDs");
        const auto lowered = lower_floating(module);
        const auto& float_function = lowered.at("functions")[1];
        const auto& float_instructions = float_function.at("blocks")[0].at("instructions");
        require(float_instructions.size() > 2 && float_instructions[0].at("debug").at("line") == 9,
            "float expansion lost its starting source boundary");
        require(float_function.at("debug_variables")[0].at("locations")[0].at("before") == float_instructions[0].at("id"),
            "float expansion failed to remap variable events");
        for (size_t i = 1; i + 1 < float_instructions.size(); ++i)
            require(!float_instructions[i].contains("debug"), "float internals became duplicate source stops");
        std::cout << "frontend debug: source locations, variable records, const types and unavailable values passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
