#include "scratch/platform.hpp"

#include <cstdlib>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#ifndef SCRATCH_INSTALL_RUNTIME_RELATIVE_DIR
#define SCRATCH_INSTALL_RUNTIME_RELATIVE_DIR "../share/scratch-llvm"
#endif

namespace scratch::platform {
namespace {
namespace fs = std::filesystem;

#ifdef _WIN32
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size == 0) throw std::runtime_error("cannot convert Windows text to UTF-8");
    std::string result(static_cast<size_t>(size), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), size, nullptr, nullptr))
        throw std::runtime_error("cannot convert Windows text to UTF-8");
    return result;
}

std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size == 0) throw std::runtime_error("invalid UTF-8 environment variable name");
    std::wstring result(static_cast<size_t>(size), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), size))
        throw std::runtime_error("cannot convert environment variable name");
    return result;
}
#endif

fs::path normalized(const fs::path& value) {
    std::error_code error;
    auto result = fs::canonical(value, error);
    if (!error) return result;
    result = fs::absolute(value, error);
    return error ? value : result.lexically_normal();
}

bool regular_file(const fs::path& value) {
    std::error_code error;
    return fs::is_regular_file(value, error);
}

FloatRuntime checked_runtime(const fs::path& bitcode, const std::string& source) {
    if (!regular_file(bitcode))
        throw std::runtime_error("floating-point runtime from " + source +
            " is missing or is not a file: " + bitcode.u8string());
    const auto license = bitcode.parent_path() / "SoftFloat-LICENSE.txt";
    if (!regular_file(license))
        throw std::runtime_error("SoftFloat license for runtime '" + bitcode.u8string() +
            "' is missing: " + license.u8string() +
            "; install SoftFloat-LICENSE.txt beside that runtime");
    // Resolve the directory without dereferencing the runtime file itself:
    // explicitly selected symlink bundles keep their adjacent license.
    return {normalized(bitcode.parent_path().empty() ? fs::path(".") : bitcode.parent_path()) /
                bitcode.filename(),
            normalized(license)};
}
} // namespace

std::vector<std::string> arguments(int argc, char** argv) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int count = 0;
    wchar_t** values = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!values) throw std::runtime_error("cannot read Windows command line");
    std::vector<std::string> result;
    try {
        result.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) result.push_back(utf8(values[i]));
    } catch (...) {
        LocalFree(values);
        throw;
    }
    LocalFree(values);
    return result;
#else
    return {argv, argv + argc};
#endif
}

std::optional<std::string> environment(const std::string& name) {
#ifdef _WIN32
    const auto key = wide(name);
    DWORD size = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
    while (size != 0) {
        std::vector<wchar_t> buffer(size);
        const DWORD length = GetEnvironmentVariableW(key.c_str(), buffer.data(), size);
        if (length == 0) return std::nullopt;
        if (length < size) return utf8(std::wstring(buffer.data(), length));
        size = length;
    }
    return std::nullopt;
#else
    if (const auto* value = std::getenv(name.c_str())) return std::string(value);
    return std::nullopt;
#endif
}

fs::path executable_path(const std::string& argv0) {
#ifdef _WIN32
    std::vector<wchar_t> buffer(1024);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0) break;
        if (length < buffer.size()) return normalized(fs::path(std::wstring(buffer.data(), length)));
        if (buffer.size() >= 32768) break;
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 1024;
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        buffer.resize(size);
        if (_NSGetExecutablePath(buffer.data(), &size) != 0)
            throw std::runtime_error("cannot determine the executable path");
    }
    return normalized(fs::u8path(buffer.data()));
#elif defined(__linux__)
    std::vector<char> buffer(1024);
    for (;;) {
        const auto length = readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (length < 0) break;
        if (static_cast<size_t>(length) < buffer.size())
            return normalized(fs::u8path(std::string(buffer.data(), static_cast<size_t>(length))));
        buffer.resize(buffer.size() * 2);
    }
#endif
    const auto supplied = fs::u8path(argv0);
    if (supplied.has_parent_path() && regular_file(supplied)) return normalized(supplied);
    if (!supplied.empty() && !supplied.has_parent_path()) {
        if (const auto search = environment("PATH")) {
#ifdef _WIN32
            constexpr char separator = ';';
#else
            constexpr char separator = ':';
#endif
            size_t start = 0;
            do {
                const auto end = search->find(separator, start);
                const auto entry = search->substr(start, end == std::string::npos ? end : end - start);
                const auto candidate = fs::u8path(entry.empty() ? "." : entry) / supplied;
                if (regular_file(candidate)) return normalized(candidate);
#ifdef _WIN32
                auto with_extension = candidate;
                with_extension += L".exe";
                if (regular_file(with_extension)) return normalized(with_extension);
#endif
                if (end == std::string::npos) break;
                start = end + 1;
            } while (start <= search->size());
        }
    }
    throw std::runtime_error("cannot determine the executable location from '" + argv0 + "'");
}

FloatRuntime find_float_runtime(const fs::path& executable,
    const std::string& explicit_file, const std::string& explicit_directory,
    const std::string& environment_directory) {
    if (!explicit_file.empty())
        return checked_runtime(fs::u8path(explicit_file), "--float-runtime");
    if (!explicit_directory.empty())
        return checked_runtime(fs::u8path(explicit_directory) / "scratch-float.bc", "--runtime-dir");
    if (!environment_directory.empty())
        return checked_runtime(fs::u8path(environment_directory) / "scratch-float.bc", "SCRATCH_RUNTIME_DIR");
    const auto directory = normalized(executable).parent_path();
    const std::vector<fs::path> directories = {
        directory,
        directory / fs::u8path(SCRATCH_INSTALL_RUNTIME_RELATIVE_DIR)
    };
    std::string searched;
    for (const auto& candidate : directories) {
        const auto bitcode = (candidate / "scratch-float.bc").lexically_normal();
        if (regular_file(bitcode)) return checked_runtime(bitcode, "executable-relative search");
        searched += "\n  " + bitcode.u8string();
    }
    throw std::runtime_error("floating-point runtime is missing; searched:" + searched +
        "\nInstall the runtime, pass --runtime-dir/--float-runtime, or set SCRATCH_RUNTIME_DIR. "
        "Keep SoftFloat-LICENSE.txt beside scratch-float.bc.");
}

} // namespace scratch::platform
