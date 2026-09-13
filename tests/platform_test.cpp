#include "scratch/platform.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

#ifndef SCRATCH_INSTALL_RUNTIME_RELATIVE_DIR
#define SCRATCH_INSTALL_RUNTIME_RELATIVE_DIR "../share/scratch-llvm"
#endif

namespace fs = std::filesystem;
namespace platform = scratch::platform;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void write_file(const fs::path& path) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot create test file: " + path.u8string());
    file << "test fixture";
}

void bundle(const fs::path& directory, const std::string& name = "scratch-float.bc") {
    write_file(directory / name);
    write_file(directory / "SoftFloat-LICENSE.txt");
}

void expect_error(const std::function<void()>& call, const std::string& fragment) {
    try {
        call();
    } catch (const std::runtime_error& error) {
        if (std::string(error.what()).find(fragment) != std::string::npos) return;
        throw std::runtime_error("wrong diagnostic: " + std::string(error.what()));
    }
    throw std::runtime_error("expected error containing: " + fragment);
}

struct TemporaryDirectory {
    fs::path path;
    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};
} // namespace

int main(int argc, char** argv) {
    try {
        const auto args = platform::arguments(argc, argv);
        require(args.size() >= 3, "expected executable path and test output directory");
        const auto expected_executable = fs::canonical(fs::u8path(args[1]));
        require(platform::executable_path("a-deliberately-invalid-argv-zero") == expected_executable,
            "executable identity must not depend on argv[0] or the current directory");
        if (args.size() > 3)
            require(args[3] == u8"é 中文 \"quoted\"", "UTF-8 command-line argument was changed");
        require(!platform::environment("SCRATCH_PLATFORM_TEST_VARIABLE_THAT_IS_NOT_DEFINED_97B2"),
            "absent environment variables must remain absent");

        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        TemporaryDirectory temporary{fs::absolute(fs::u8path(args[2])) /
            fs::u8path(std::string(u8"platform 中文-") + std::to_string(stamp))};
        fs::create_directories(temporary.path);
        const auto executable = temporary.path / "prefix/bin/scratch-llvm";
        const auto installed = (executable.parent_path() /
            fs::u8path(SCRATCH_INSTALL_RUNTIME_RELATIVE_DIR)).lexically_normal();
        const auto explicit_dir = temporary.path / "explicit";
        const auto environment_dir = temporary.path / "environment";
        const auto explicit_file_dir = temporary.path / "custom-file";
        const auto missing_dir = temporary.path / "missing";
        write_file(executable);

        expect_error([&] { platform::find_float_runtime(executable); }, "searched:");
        bundle(installed);
        auto runtime = platform::find_float_runtime(executable);
        require(fs::equivalent(runtime.bitcode, installed / "scratch-float.bc"),
            "installed runtime directory was not found");

        bundle(executable.parent_path());
        runtime = platform::find_float_runtime(executable);
        require(fs::equivalent(runtime.bitcode, executable.parent_path() / "scratch-float.bc"),
            "adjacent runtime must precede installed runtime");

        bundle(environment_dir);
        runtime = platform::find_float_runtime(executable, {}, {}, environment_dir.u8string());
        require(fs::equivalent(runtime.bitcode, environment_dir / "scratch-float.bc"),
            "environment runtime must precede automatic discovery");

        bundle(explicit_dir);
        runtime = platform::find_float_runtime(executable, {}, explicit_dir.u8string(), environment_dir.u8string());
        require(fs::equivalent(runtime.bitcode, explicit_dir / "scratch-float.bc"),
            "explicit runtime directory must precede environment");

        bundle(explicit_file_dir, "chosen.bc");
        runtime = platform::find_float_runtime(executable, (explicit_file_dir / "chosen.bc").u8string(),
            explicit_dir.u8string(), environment_dir.u8string());
        require(fs::equivalent(runtime.bitcode, explicit_file_dir / "chosen.bc"),
            "explicit runtime file must precede runtime directory");
        require(fs::equivalent(runtime.license, explicit_file_dir / "SoftFloat-LICENSE.txt"),
            "runtime license must accompany the selected file");

        expect_error([&] { platform::find_float_runtime(executable, (missing_dir / "chosen.bc").u8string(),
            explicit_dir.u8string(), environment_dir.u8string()); }, "--float-runtime");
        expect_error([&] { platform::find_float_runtime(executable, {}, missing_dir.u8string(),
            environment_dir.u8string()); }, "--runtime-dir");
        expect_error([&] { platform::find_float_runtime(executable, {}, {}, missing_dir.u8string()); },
            "SCRATCH_RUNTIME_DIR");

        fs::remove(explicit_file_dir / "SoftFloat-LICENSE.txt");
        expect_error([&] { platform::find_float_runtime(executable, (explicit_file_dir / "chosen.bc").u8string()); },
            "SoftFloat license");
        fs::remove(executable.parent_path() / "SoftFloat-LICENSE.txt");
        expect_error([&] { platform::find_float_runtime(executable); }, "SoftFloat license");

        std::cout << "Platform and runtime discovery tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "platform-test: " << error.what() << '\n';
        return 1;
    }
}
