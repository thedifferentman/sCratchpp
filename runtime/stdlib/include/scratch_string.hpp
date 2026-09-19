#pragma once
#include <string>

namespace scratch {
// Copy UTF-8 text into the native Scratch variable __scl_string. Embedded NUL
// is preserved. Every malformed UTF-8 byte is replaced by U+FFFD.
// U+0008 is also replaced: vanilla Scratch strips it while loading projects.
// Single execution context: the next call replaces the previous string.
void set_string(const std::string& text);
}
