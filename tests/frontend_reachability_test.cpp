#include "scratch/frontend.hpp"
#include "scratch/prune.hpp"
#include "scratch/platform.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

using namespace scratch;

static void require(bool condition, const char* message) {
    if (!condition) throw Error(message);
}
static void write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    if (!file) throw Error("cannot create frontend test fixture");
    file << text;
}
static std::set<std::string> names(const Json& module, const std::string& category) {
    std::set<std::string> result;
    for (const auto& value : module.at(category)) result.insert(value.at("name").get<std::string>());
    return result;
}

int main(int argc, char** argv) {
    try {
        const auto args = platform::arguments(argc, argv);
        if (args.size() != 2) throw Error("usage: frontend-reachability-test <scratch-output-directory>");
        const auto directory = std::filesystem::u8path(args[1]);
        std::filesystem::create_directories(directory);
        const auto path = directory / "reachability.ll";
        write(path, R"IR(
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
@targets = global [2 x ptr] [ptr blockaddress(@jump, %first), ptr blockaddress(@jump, %second)]
@kept_data = global i32 11
@dead_data = global i32 99
@llvm.used = appending global [1 x ptr] [ptr @kept_data], section "llvm.metadata"
@llvm.global_ctors = appending global [1 x {i32, ptr, ptr}] [{i32,ptr,ptr} {i32 100,ptr @ctor,ptr null}]
define void @ctor() { ret void }
define i32 @main() {
entry:
  %address = load ptr, ptr @targets
  %result = call i32 @jump(ptr %address)
  ret i32 %result
}
define i32 @jump(ptr %address) {
entry:
  indirectbr ptr %address, [label %first, label %second]
first:
  ret i32 31
second:
  ret i32 32
}
declare i32 @personality(...)
declare i32 @external_call()
define i32 @dead_eh() personality ptr @personality {
entry:
  %value = invoke i32 @external_call() to label %normal unwind label %exception
normal:
  ret i32 %value
exception:
  %exception_value = landingpad {ptr,i32} cleanup
  resume {ptr,i32} %exception_value
}
)IR");
        FrontendOptions options;
        options.entry_points = {"main"};
        const auto module = read_modules({path.u8string()}, options);
        require(names(module, "functions") == std::set<std::string>({"main", "jump", "ctor"}),
            "LLVM-level reachability failed to remove unused EH function");
        require(names(module, "globals") == std::set<std::string>({"targets", "kept_data", "llvm.global_ctors"}),
            "LLVM-level initializer/used roots are incorrect");
        require(module.at("declarations").empty(), "unreachable EH declarations were retained");
        const auto& targets = module.at("globals")[0].at("initializer").at("elements");
        require(targets[0].at("kind") == "blockaddress" && targets[0].at("function") == "jump" &&
            targets[0].at("block") == "b1" && targets[1].at("block") == "b2", "global blockaddress IDs are unstable");
        const auto& jump = module.at("functions")[2];
        require(jump.at("name") == "jump", "fixture function ordering changed");
        const auto& indirect = jump.at("blocks")[0].at("instructions")[0];
        require(indirect.at("op") == "indirectbr" && indirect.at("targets") == Json::array({"b1", "b2"}),
            "indirectbr targets are incorrect");
        const auto pruned = prune_module(module);
        require(names(pruned, "functions") == names(module, "functions"), "JSON pruning lost blockaddress ownership");
        bool rejected = false;
        try { (void)read_modules({path.u8string()}); } catch (const Error&) { rejected = true; }
        require(rejected, "empty entry_points must retain and diagnose unsupported definitions");

        const auto assembly_path = directory / "dead-assembly.ll";
        write(assembly_path, R"IR(
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
define i32 @main() { ret i32 0 }
define void @dead_asm() { call void asm sideeffect "mov eax, ebx", ""()
ret void }
)IR");
        bool checked_assembly = false;
        options.asm_validator = [&](const std::string& text) {
            checked_assembly = true;
            if (text == "mov eax, ebx") throw Error("test machine instruction rejected");
        };
        rejected = false;
        try { (void)read_modules({assembly_path.u8string()}, options); } catch (const Error&) { rejected = true; }
        require(checked_assembly && rejected, "unreachable machine assembly bypassed upfront validation");

        const auto indirect_path = directory / "address-taken.ll";
        write(indirect_path, R"IR(
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
@unused_pointer = global ptr @address_visible
define i32 @main(ptr %target) { %result = call i32 %target()
ret i32 %result }
define i32 @address_visible() { ret i32 3 }
define i32 @address_argument(ptr %p) { ret i32 5 }
define i32 @dead_registration() { %x = call i32 @address_argument(ptr @address_argument)
ret i32 %x }
define i32 @unreferenced() { ret i32 4 }
)IR");
        auto indirect_module = read_modules({indirect_path.u8string()}, options);
        require(names(indirect_module, "functions") == std::set<std::string>({"main", "address_visible", "address_argument"}),
            "indirect call did not conservatively retain address-visible function");
        require(indirect_module.at("globals").empty(), "unused source global should remain removable");
        require(names(prune_module(indirect_module), "functions") == names(indirect_module, "functions"),
            "JSON pruning discarded a conservative LLVM-level address candidate");
        const auto bundle_path = directory / "assume-align.ll";
        write(bundle_path, R"IR(
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
declare void @llvm.assume(i1)
declare void @llvm.experimental.noalias.scope.decl(metadata)
define i32 @main() {
  %p = alloca i32, align 64
  call void @llvm.assume(i1 true) ["align"(ptr %p, i64 64)]
  call void @llvm.experimental.noalias.scope.decl(metadata !0)
  ret i32 0
}
!0 = !{!1}
!1 = distinct !{!1, !2}
!2 = distinct !{!2}
)IR");
        (void)read_modules({bundle_path.u8string()},options);
        write(bundle_path, R"IR(
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
declare void @external()
define i32 @main() {
  call void @external() ["deopt"(i32 1)]
  ret i32 0
}
)IR");
        rejected=false;
        try {(void)read_modules({bundle_path.u8string()},options);} catch(const Error&) {rejected=true;}
        require(rejected,"stateful call bundle bypassed validation");
        write(bundle_path, R"IR(
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
declare void @llvm.experimental.noalias.scope.decl(metadata)
define i32 @main() {
  call void @llvm.experimental.noalias.scope.decl(metadata !0) ["deopt"(i32 1)]
  ret i32 0
}
!0 = !{!1}
!1 = distinct !{!1, !2}
!2 = distinct !{!2}
)IR");
        rejected=false;
        try {(void)read_modules({bundle_path.u8string()},options);} catch(const Error&) {rejected=true;}
        require(rejected,"stateful bundle hidden on metadata-only intrinsic bypassed validation");
        const auto whole_path=directory/"whole-program.ll";
        write(whole_path,R"IR(
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
@callback = global ptr @kept_callback
@dead_address = global ptr @unused
@llvm.global_ctors = appending global [1 x {i32,ptr,ptr}] [{i32,ptr,ptr} {i32 100,ptr @ctor,ptr null}]
@llvm.global_dtors = appending global [1 x {i32,ptr,ptr}] [{i32,ptr,ptr} {i32 100,ptr @dtor,ptr null}]
@llvm.used = appending global [1 x ptr] [ptr @explicitly_used], section "llvm.metadata"
@trace = global i32 0
define internal i32 @main() {
  %f = load volatile ptr, ptr @callback
  %r = call i32 %f()
  ret i32 %r
}
define i32 @kept_callback() { ret i32 7 }
define i32 @unused() { ret i32 99 }
define void @ctor() { store volatile i32 1, ptr @trace
ret void }
define void @dtor() { store volatile i32 2, ptr @trace
ret void }
define void @explicitly_used() { ret void }
define i32 @__sclr_test_delayed(i32 %x) { %r = add i32 %x, 1
ret i32 %r }
)IR");
        FrontendOptions whole;
        whole.whole_program=true;
        whole.passes="default<O2>";
        whole.entry_points={"main","__sclr_test_delayed"};
        auto optimized=read_modules({whole_path.u8string()},whole);
        const auto kept=names(optimized,"functions");
        for(const char* name:{"main","kept_callback","ctor","dtor","explicitly_used","__sclr_test_delayed"})
            require(kept.count(name)!=0,"whole-program pruning discarded a required entry/callback/initializer");
        require(!kept.count("unused"),"unused address-taken function escaped whole-program pruning");
        require(optimized.at("whole_program")==true,"whole-program mode not recorded");
        whole.entry_points.clear();rejected=false;
        try {(void)read_modules({whole_path.u8string()},whole);}catch(const Error&){rejected=true;}
        require(rejected,"whole-program mode accepted no roots");
        std::cout << "frontend reachability tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
