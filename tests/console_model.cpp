#include "console/console.hpp"

#ifdef SCRPP_CONSOLE_STUB_FONT
namespace scratch::pte {
void draw_cell(unsigned cp, int, int, int, int, unsigned) {
    asm volatile("data_addtolist LIST=\"console_test_draws\" ITEM=%0" : : "r"(cp) : "memory");
}
}
static int draw_count() {
    int count;
    asm volatile("data_lengthoflist LIST=\"console_test_draws\"" : "=r"(count) : : "memory");
    return count;
}
#endif

int main() {
    using namespace scratch::console;
    Options options;
    options.columns = 8;
    options.rows = 2;
    options.history_lines = 3;
    init(options);
    write("A\xe4", 2);
    flush(); // Incomplete UTF-8 survives an explicit flush.
#ifdef SCRPP_CONSOLE_STUB_FONT
    if (draw_count() != 1) return 17;
#endif
    if (cell(0, 0) != 'A' || cursor_column() != 1) return 1;
    write("\xb8\xad" "B", 3);
    if (cell(0, 1) != 0x4e2d || cell(0, 2) != 0 || cell(0, 3) != 'B') return 2;
    if (cursor_column() != 4) return 3;
    flush();
#ifdef SCRPP_CONSOLE_STUB_FONT
    if (draw_count() != 3) return 18; // A must not be redrawn on append.
#endif
    write("\r12"); // Overwrite the leading half of a wide character.
    if (cell(0, 0) != '1' || cell(0, 1) != '2' || cell(0, 2) != 0 || cell(0, 3) != 'B') return 4;
    flush();
#ifdef SCRPP_CONSOLE_STUB_FONT
    if (draw_count() != 5) return 19; // Only the changed span (1, 2) is repainted; B stays intact.
#endif
    write("\b");
    if (cell(0, 1) != 0 || cursor_column() != 1) return 5;
    write("\r\xe4\xb8\xad");
    write("\b");
    if (cell(0, 0) != 0 || cell(0, 1) != 0 || cursor_column() != 0) return 6;
    clear();
    write("12345678\n"); // Exact width followed by newline must not double-wrap.
    if (line_count() != 2 || cursor_column() != 0) return 7;
    write("x\tY");
    if (line_count() != 3 || cell(2, 0) != 'Y' || first_visible_line() != 1) return 8;
    write("\nZ");
    if (line_count() != 3 || cell(0, 0) != 'x' || cell(1, 0) != 'Y' || cell(2, 0) != 'Z') return 9;
    scroll(1);
    if (first_visible_line() != 0) return 10;
    scroll(-1000);
    if (first_visible_line() != 1) return 11;
    asm volatile("data_addtolist LIST=\"__scl_events::wheel\" ITEM=1" : : : "memory");
    flush();
    if (first_visible_line() != 0) return 12;
    clear();
    write("\xed\xa0\x80", 3); // Surrogate scalar rejected.
    if (cell(0, 0) != 0xfffd) return 13;
    write("\xc2" "A", 2); // Bad continuation is reprocessed.
    if (cell(0, 1) != 0xfffd || cell(0, 2) != 'A') return 14;
    writeln("\xe4"); // Newline terminates an incomplete sequence.
    if (cell(0, 3) != 0xfffd || line_count() != 2) return 15;
    write("\xf0\x9f\x98\x80", 4); // Non-BMP remains one scalar, two columns.
    if (cell(1, 0) != 0x1f600 || cursor_column() != 2) return 16;
    flush();
    // Cursor is an overlay, never a character in the input/text buffer.
#ifdef SCRPP_CONSOLE_STUB_FONT
    init(options);
    if (!begin_input("> ")) return 20;
    const int before_cursor = draw_count();
    flush();
    int last_glyph;
    asm volatile("data_itemoflist LIST=\"console_test_draws\" INDEX=(data_lengthoflist LIST=\"console_test_draws\")" : "=r"(last_glyph) : : "memory");
    if (draw_count() != before_cursor + 2 || last_glyph != '_' || cell(0, 2) != 0 || cursor_column() != 2) return 21;
    asm volatile("data_setvariableto VARIABLE=\"__scl_console_cursor_time\" VALUE="
        "(operator_subtract NUM1=(sensing_dayssince2000) NUM2=0.00001)" : : : "memory");
    flush();
    if (draw_count() != before_cursor + 2 || cell(0, 2) != 0) return 22;
    cancel_input();
    flush();
    if (input_active()) return 23;
#endif
    return 0;
}
