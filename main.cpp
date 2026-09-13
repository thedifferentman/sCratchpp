#include "scratch/frontend.hpp"
#include "scratch/backend.hpp"
#include "scratch/float_lower.hpp"
#include "scratch/prune.hpp"
#include "scratch/assembly.hpp"
#include "scratch/platform.hpp"
#include <fstream>
#include <iostream>
#include <filesystem>

int run(const std::vector<std::string>& args) {
    try {
        std::vector<std::string> inputs;
        std::string output = "out.sb3", dump, float_runtime, runtime_directory, debug_map;
        scratch::FrontendOptions frontend;
        frontend.entry_points = {"main"};
        frontend.asm_validator = scratch::validate_assembly_template;
        scratch::BackendOptions backend;
        for (size_t i = 1; i < args.size(); ++i) {
            const std::string a = args[i];
            auto value = [&]() -> std::string { if (++i >= args.size()) throw scratch::Error("missing value for " + a); return args[i]; };
            if (a == "-o" || a == "--output") output = value();
            else if (a == "--dump-ir") dump = value();
            else if (a == "--debug-map") { debug_map=value();frontend.debug_info=true;backend.debug_info=true; }
            else if (a == "--passes") frontend.passes = value();
            else if (a == "--whole-program") frontend.whole_program = true;
            else if (a == "--data-layout") frontend.default_layout = value();
            else if (a == "--float-runtime") float_runtime = value();
            else if (a == "--runtime-dir") runtime_directory = value();
            else if (a == "--memory") {
                const auto text=value();size_t consumed=0;const auto n=std::stoull(text,&consumed);
                if(consumed!=text.size() || n<1024 || n>200000)throw scratch::Error("--memory must be an integer between 1024 and 200000");
                backend.memory_size=static_cast<unsigned>(n);
            }
            else if (a == "--arg") backend.program_args.push_back(value());
            else if (a == "--") { while(++i<args.size())backend.program_args.push_back(args[i]);break; }
            else if (a == "--version") { std::cout << "scratch-llvm 0.1.0\n"; return 0; }
            else if (a == "--help" || a == "-h") {
                std::cout << "Usage: scratch-llvm input.ll [more.bc ...] -o project.sb3\n"
                             "  --dump-ir path.json   Write normalized, typed backend input\n"
                             "  --debug-map path.json Write source/IR/block mapping without adding Scratch blocks\n"
                             "  --passes pipeline    LLVM pass pipeline (optional)\n"
                             "  --whole-program      Prune final-program exports, preserving required runtime entries\n"
                             "  --data-layout value  Explicit fallback for modules without layout\n"
                             "  --float-runtime path Override the integer-only float runtime\n"
                             "  --runtime-dir dir    Runtime directory (overrides SCRATCH_RUNTIME_DIR)\n"
                             "  --memory bytes       Program memory capacity (default 65536)\n"
                             "  --arg value          Append a UTF-8 program argument (argv[0] is program)\n"
                             "  -- args...           Remaining arguments become program arguments\n"
                             "Runtime lookup: --float-runtime, --runtime-dir, SCRATCH_RUNTIME_DIR,\n"
                             "then beside the executable and its installed share/scratch-llvm directory.\n"
                             "SoftFloat-LICENSE.txt must accompany the selected runtime.\n";
                return 0;
            } else if (!a.empty() && a[0] == '-') throw scratch::Error("unknown option: " + a);
            else inputs.push_back(a);
        }
        if (inputs.empty()) throw scratch::Error("no input modules; use --help");
        auto module = scratch::prune_module(scratch::read_modules(inputs, frontend));
        const auto dependencies = scratch::floating_dependencies(module);
        std::string float_license;
        if (!dependencies.empty()) {
            const auto runtime = scratch::platform::find_float_runtime(
                scratch::platform::executable_path(args.front()), float_runtime, runtime_directory,
                scratch::platform::environment("SCRATCH_RUNTIME_DIR").value_or(""));
            frontend.runtime_paths.push_back(runtime.bitcode.u8string());
            auto roots = dependencies; roots.push_back("main");
            frontend.entry_points = roots;
            module = scratch::prune_module(scratch::read_modules(inputs, frontend), roots);
            module = scratch::lower_floating(std::move(module));
            std::ifstream file(runtime.license, std::ios::binary);
            if (!file) throw scratch::Error("cannot read SoftFloat license: " + runtime.license.u8string());
            float_license.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        } else module = scratch::lower_floating(std::move(module));
        if (!dump.empty()) { std::ofstream f(std::filesystem::u8path(dump)); if (!f) throw scratch::Error("cannot write " + dump); f << module.dump(2) << '\n'; }
        auto project = scratch::compile(module, backend);
        if (!float_license.empty()) project.files["licenses/SoftFloat.txt"] = float_license;
        project.save(output);
        if(!debug_map.empty()) {
            std::ofstream f(std::filesystem::u8path(debug_map),std::ios::binary);
            if(!f)throw scratch::Error("cannot write debug map: "+debug_map);
            f<<project.debug_map.dump(2)<<'\n';f.flush();
            if(!f)throw scratch::Error("failed writing debug map: "+debug_map);
        }
        std::cout << "Wrote " << output << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "scratch-llvm: error: " << e.what() << '\n';
        return 1;
    }
}

int main(int argc, char** argv) {
    try {
        return run(scratch::platform::arguments(argc, argv));
    } catch (const std::exception& e) {
        std::cerr << "scratch-llvm: error: " << e.what() << '\n';
        return 1;
    }
}
