// Declare the console package in sCrpp.toml, then copy this file to src/main.cpp.
#include <console/console.hpp>
#include <scratch.hpp>

int main() {
    namespace console = scratch::console;
    console::init();
    console::writeln("Console input / 键盘输入");
    if (scratch::is_turbowarp()) {
        console::writeln("Enter submits; Backspace deletes.");
        console::writeln("Hold Shift for capitals; quit exits.");
    } else {
        console::writeln("Enter submits; \\ deletes; QUIT exits.");
        console::writeln("Letters are uppercase in Scratch.");
    }
    for (;;) {
        const std::string line = console::read_line("> ", 256);
        if (line == "QUIT" || line == "quit") break;
        console::writeln("You typed: " + line);
    }
    console::writeln("Goodbye.");
    console::flush();
    return 0;
}
