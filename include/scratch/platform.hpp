#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace scratch::platform {

// UTF-8 is the compiler's boundary encoding, including on Windows. POSIX
// command-line bytes are preserved, as required by the native process ABI.
std::vector<std::string> arguments(int argc, char** argv);
std::optional<std::string> environment(const std::string& name);

// Prefer the operating system's executable identity. Canonicalize symlinks and
// use argv[0]/PATH only as a fallback; never interpret a bare argv[0] as a path
// relative to the caller's arbitrary current directory.
std::filesystem::path executable_path(const std::string& argv0);

struct FloatRuntime {
    std::filesystem::path bitcode;
    std::filesystem::path license;
};

// Explicit overrides are authoritative: an invalid override is an error, not
// a reason to silently load a different runtime. A runtime and its license
// always come from the same directory. Empty environment values are ignored.
// Priority: explicit file, explicit directory, environment directory,
// executable directory, installed data directory relative to the executable.
FloatRuntime find_float_runtime(
    const std::filesystem::path& executable,
    const std::string& explicit_file = {},
    const std::string& explicit_directory = {},
    const std::string& environment_directory = {});

} // namespace scratch::platform
