// Declare the console package in sCrpp.toml, then copy this file to src/main.cpp.
#include <console/console.hpp>

int main() {
    scratch::console::init();
    scratch::console::writeln("Hello from C++!");
    scratch::console::writeln("中文测试：你好，世界！");
    for (int i = 1; i <= 60; ++i) {
        char line[] = "Line 00: scroll to inspect history";
        line[5] = static_cast<char>('0' + i / 10);
        line[6] = static_cast<char>('0' + i % 10);
        scratch::console::writeln(line);
    }
    // Keep servicing the UI and wheel queue. Click the Scratch stop button to exit.
    for (;;) scratch::console::flush();
}
