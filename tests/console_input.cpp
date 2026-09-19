#include <console/console.hpp>
#include <events/events.hpp>
#include <scratch_platform.hpp>

// Input-model test: the real keyboard hats and events library are linked.
// Glyph requests are recorded instead of loading the 5 MB font resource.
namespace scratch::pte {
void draw_cell(unsigned cp, int, int, int, int, unsigned) {
    asm volatile("data_addtolist LIST=\"console_input_draws\" ITEM=%0" : : "r"(cp) : "memory");
}
}

static void phase(int number) {
    asm volatile("data_setvariableto VARIABLE=\"console_input_phase\" VALUE=%0" : : "r"(number) : "memory");
}
static void collect_batch(int number) {
    phase(number);
    for (;;) {
        int ready;
        asm volatile("data_variable VARIABLE=\"console_input_go\"" : "=r"(ready) : : "memory");
        if (ready == number) break;
        scratch::events::yield(); // Collect hats without dispatching callbacks yet.
    }
    scratch::console::flush();
}
static int callback_errors = 0;
static int enter_callbacks = 0;
static void callback_probe(int key, void*) {
    if (key != 13) return;
    ++enter_callbacks;
    if (scratch::console::begin_input("BAD")) ++callback_errors;
    if (!scratch::console::read_line("BAD").empty()) ++callback_errors;
}

int main() {
    using namespace scratch::console;
    const bool tw = scratch::is_turbowarp();
    asm volatile("data_setvariableto VARIABLE=\"console_input_tw\" VALUE=%0" : : "r"(static_cast<int>(tw)) : "memory");
    Options options;
    options.columns = 8;
    options.rows = 2;
    options.history_lines = 3;
    init(options);
    if (!scratch::events::on_key(callback_probe)) return 1;
    std::string line = "unchanged";
    if (try_read_line(line) || line != "unchanged") return 2;
    if (!begin_input("> ") || !input_active() || begin_input("BAD")) return 3;
    collect_batch(1); // a,B,backslash,c,space,1,Enter,z in one poll batch.
    if (input_active() || !try_read_line(line) || line != (tw ? "aB\\c 1" : "AC 1")) return 4;
    if (try_read_line(line) || line != (tw ? "aB\\c 1" : "AC 1") || callback_errors || enter_callbacks != 1) return 5;
    if (cell(1, 0) != 0) return 6; // z after Enter must not be echoed.

    clear();
    if (!begin_input("1234567")) return 7;
    collect_batch(2); // Erase on empty, a,B,c,erase,erase,D,Enter.
    if (!try_read_line(line) || line != (tw ? "aD" : "AD")) return 8;
    if (cell(0, 0) != '1' || cell(0, 6) != '7' || cell(0, 7) != (tw ? 'a' : 'A') || cell(1, 0) != 'D') return 9;

    options.columns = 4;
    options.rows = 1;
    options.history_lines = 1;
    init(options);
    if (!begin_input("P", 1024)) return 10;
    collect_batch(3); // Retained capacity is 3: a,b,c,d,e,erase,d,Enter.
    if (!try_read_line(line) || line != (tw ? "abd" : "ABD")) return 11;
    if (!begin_input("X")) return 12;
    write("OUT"); // Explicit external output cancels input.
    if (input_active() || try_read_line(line)) return 13;
    if (!begin_input("X")) return 14;
    cancel_input();
    if (input_active() || try_read_line(line)) return 15;

    options.columns = 8;
    options.rows = 2;
    options.history_lines = 8;
    init(options);
    phase(4);
    line = read_line("BLOCK> "); // Host sends x,Y,Enter while this waits.
    if (line != (tw ? "xY" : "XY") || input_active()) return 16;

    clear();
    if (!begin_input("")) return 17;
    collect_batch(5); // a,b,Backspace,c,backslash,Enter.
    if (!try_read_line(line) || line != (tw ? "ac\\" : "AB")) return 18;
    if (!begin_input("EMPTY> ", 0)) return 19;
    collect_batch(6); // a,Enter: byte limit zero still accepts an empty line.
    if (!try_read_line(line) || !line.empty() || callback_errors || enter_callbacks != 6) return 20;

    if (!begin_input("discard")) return 21;
    clear();
    if (input_active() || try_read_line(line)) return 22;
    if (input_status() != InputStatus::idle && input_status() != InputStatus::cancelled) return 23;
    if (!begin_input("")) return 24;
    std::string sentinel="unchanged";
    if (read_line_result(sentinel) != InputStatus::busy || sentinel != "unchanged") return 25;
    cancel_input();
    if (try_read_line_result(sentinel) != InputStatus::cancelled || input_status() != InputStatus::idle) return 26;
    close_input();
    if (begin_input() || read_line_result(sentinel) != InputStatus::eof || sentinel != "unchanged") return 27;
    reopen_input();
    if (input_status() != InputStatus::idle) return 28;
    phase(7);
    return 0;
}
