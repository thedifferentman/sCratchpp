#include "console.hpp"
#include <pte/pte.hpp>
#include <triangle/triangle.hpp>
#include <events/events.hpp>
#include <scratch_platform.hpp>
#include <utility>

namespace scratch::console {
namespace {
Options config;
bool initialized = false;
bool repaint = true;
bool flushing = false;
int wheel_handler = 0;
int key_handler = 0;
int oldest = 0;
int newest = 0;
int view = 0;
int column = 0;
unsigned utf_value = 0;
unsigned utf_min = 0;
int utf_remaining = 0;
bool accepting_input = false;
bool cursor_visible = false;
bool cursor_drawn = false;
int cursor_screen_row = -1, cursor_screen_col = -1;
bool reset_surface = true;
Options painted_config;
void dirty(int row, int col);
void reset_cursor_clock() {
    asm volatile("data_setvariableto VARIABLE=\"__scl_console_cursor_time\" VALUE=(sensing_dayssince2000)" : : : "memory");
}
void reset_cursor() { cursor_visible = true; reset_cursor_clock(); }
bool cursor_due() {
    bool due;
    asm volatile("operator_or OPERAND1=(operator_not OPERAND=(operator_lt OPERAND1="
        "(operator_multiply NUM1=(operator_subtract NUM1=(sensing_dayssince2000) NUM2=(data_variable VARIABLE=\"__scl_console_cursor_time\")) NUM2=86400000) OPERAND2=500)) "
        "OPERAND2=(operator_lt OPERAND1=(sensing_dayssince2000) OPERAND2=(data_variable VARIABLE=\"__scl_console_cursor_time\"))"
        : "=r"(due) : : "memory");
    return due;
}
bool turbowarp_input = false;
bool completed_input = false;
bool input_closed = false;
bool input_cancelled = false;
std::string& input_text() { static std::string text; return text; }
std::string& completed_text() { static std::string text; return text; }
std::size_t input_limit = 0;
int input_row = 0;
int input_column = 0;

int clamp(int value, int low, int high) {
    return value < low ? low : (value > high ? high : value);
}
int row_height(int size) { return size + 2 * ((size + 31) / 32) + 2; }
int bottom_view() {
    const int candidate = newest - config.rows + 1;
    return candidate < oldest ? oldest : candidate;
}
int index_of(int row, int col) {
    return (row % config.history_lines) * config.columns + col + 1;
}
int read_cell(int row, int col) {
    int result;
    const int index = index_of(row, col);
    asm volatile("data_itemoflist LIST=\"__scl_console::cells\" INDEX=%1"
                 : "=r"(result) : "r"(index) : "memory");
    return result;
}
void set_cell(int row, int col, int cp) {
    const int index = index_of(row, col);
    asm volatile("data_replaceitemoflist LIST=\"__scl_console::cells\" INDEX=%0 ITEM=%1"
                 : : "r"(index), "r"(cp) : "memory");
}
void clear_dirty() {
    asm volatile("data_deletealloflist LIST=\"__scl_console::dirty_rows\";"
                 "data_deletealloflist LIST=\"__scl_console::dirty_cols\";"
                 : : : "memory");
}
void dirty(int row, int col) {
    if (repaint) return;
    asm volatile("data_addtolist LIST=\"__scl_console::dirty_rows\" ITEM=%0;"
                 "data_addtolist LIST=\"__scl_console::dirty_cols\" ITEM=%1;"
                 : : "r"(row), "r"(col) : "memory");
}
void clear_row(int row) {
    const int index = index_of(row, 0);
    asm volatile(
        "data_setvariableto VARIABLE=\"__scl_console_clear_i\" VALUE=%0;"
        "control_repeat TIMES=%1 SUBSTACK %{"
        " data_replaceitemoflist LIST=\"__scl_console::cells\" INDEX=(data_variable VARIABLE=\"__scl_console_clear_i\") ITEM=0;"
        " data_changevariableby VARIABLE=\"__scl_console_clear_i\" VALUE=1; %};"
        : : "r"(index), "r"(config.columns) : "memory");
}
void next_line() {
    const bool follow = view == bottom_view();
    ++newest;
    if (newest - oldest >= config.history_lines) ++oldest;
    clear_row(newest);
    column = 0;
    const int old_view = view;
    if (follow) view = bottom_view();
    else if (view < oldest) view = oldest;
    if (old_view != view) repaint = true;
}
// Match the common terminal width rules without claiming shaping, grapheme
// clustering, or a complete Unicode wcwidth implementation.
int character_width(unsigned cp) {
    return (cp >= 0x1100 && (cp <= 0x115f || cp == 0x2329 || cp == 0x232a ||
            (cp >= 0x2e80 && cp <= 0xa4cf && cp != 0x303f) ||
            (cp >= 0xac00 && cp <= 0xd7a3) || (cp >= 0xf900 && cp <= 0xfaff) ||
            (cp >= 0xfe10 && cp <= 0xfe19) || (cp >= 0xfe30 && cp <= 0xfe6f) ||
            (cp >= 0xff00 && cp <= 0xff60) || (cp >= 0xffe0 && cp <= 0xffe6) ||
            (cp >= 0x1f300 && cp <= 0x1faff) || (cp >= 0x20000 && cp <= 0x3fffd))) ? 2 : 1;
}
void erase_at(int row, int col) {
    const int old = read_cell(row, col);
    if (!old) return;
    dirty(row, col);
    if (old == -1 && col > 0) { dirty(row, col - 1); set_cell(row, col - 1, 0); }
    if (old > 0 && col + 1 < config.columns && read_cell(row, col + 1) == -1) {
        dirty(row, col + 1); set_cell(row, col + 1, 0);
    }
    set_cell(row, col, 0);
}
void put(unsigned cp) {
    if (cp == '\n') { next_line(); return; }
    if (cp == '\r') { column = 0; return; }
    if (cp == '\b') {
        if (column > 0) {
            --column;
            if (read_cell(newest, column) == -1 && column > 0) --column;
            erase_at(newest, column);
        }
        return;
    }
    if (cp == '\t') {
        const int spaces = 8 - (column % 8);
        for (int i = 0; i < spaces; ++i) put(' ');
        return;
    }
    if (cp < 32 || (cp >= 0x7f && cp < 0xa0)) return;
    const int width = character_width(cp);
    if (column + width > config.columns) next_line();
    erase_at(newest, column);
    if (width == 2) erase_at(newest, column + 1);
    set_cell(newest, column, static_cast<int>(cp));
    if (width == 2) set_cell(newest, column + 1, -1);
    dirty(newest, column);
    column += width;
}
void byte(unsigned char value) {
    if (utf_remaining) {
        if ((value & 0xc0) == 0x80) {
            utf_value = (utf_value << 6) | (value & 0x3f);
            if (--utf_remaining == 0) {
                const unsigned cp = utf_value;
                put(cp < utf_min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ? 0xfffd : cp);
            }
            return;
        }
        utf_remaining = 0;
        put(0xfffd);
        // Reprocess the non-continuation byte as a new sequence.
    }
    if (value < 0x80) put(value);
    else if (value >= 0xc2 && value <= 0xdf) { utf_value = value & 31; utf_min = 0x80; utf_remaining = 1; }
    else if (value >= 0xe0 && value <= 0xef) { utf_value = value & 15; utf_min = 0x800; utf_remaining = 2; }
    else if (value >= 0xf0 && value <= 0xf4) { utf_value = value & 7; utf_min = 0x10000; utf_remaining = 3; }
    else put(0xfffd);
}
void snapshot_position() {
    asm volatile("data_setvariableto VARIABLE=\"__scl_console_saved_x\" VALUE=(motion_xposition);"
                 "data_setvariableto VARIABLE=\"__scl_console_saved_y\" VALUE=(motion_yposition);"
                 : : : "memory");
}
void restore_position() {
    asm volatile("pen_penUp; motion_gotoxy X=(data_variable VARIABLE=\"__scl_console_saved_x\")"
                 " Y=(data_variable VARIABLE=\"__scl_console_saved_y\");" : : : "memory");
}
void timing_refresh() {
    restore_position();
    scratch::events::refresh();
}
void wheel_scroll(int direction, void*) { scroll(direction); }

void key_input(int key, void*) {
    if (!accepting_input) return;
    if ((key >= 32 && key <= 126) || key == 8 || key == 13) {
        if (view != bottom_view()) { view = bottom_view(); repaint = true; }
    }
    if (key == 13) {
        cursor_visible = false;
        accepting_input = false;
        completed_input = true;
        completed_text() = std::move(input_text());
        put('\n');
        return;
    }
    if (key == (turbowarp_input ? 8 : '\\')) {
        reset_cursor();
        if (input_text().empty()) return;
        const int offset = input_column + static_cast<int>(input_text().size()) - 1;
        erase_at(input_row + offset / config.columns, offset % config.columns);
        input_text().pop_back();
        const int end = input_column + static_cast<int>(input_text().size());
        newest = input_row + (end ? (end - 1) / config.columns : 0);
        column = end ? (end - 1) % config.columns + 1 : 0;
        if (view > bottom_view()) view = bottom_view();
        return;
    }
    if (key < 32 || key > 126 || input_text().size() >= input_limit) return;
    input_text().push_back(static_cast<char>(key));
    put(static_cast<unsigned>(key));
    reset_cursor();
}

void draw(int row, int col) {
    if (row < view || row >= view + config.rows || row < oldest || row > newest) return;
    const int cp = read_cell(row, col);
    if (cp <= 0 || cp == ' ') return;
    const int cell_width = (config.font_size + 1) / 2;
    const int x = config.x + col * cell_width;
    const int y = config.y - (row - view) * row_height(config.font_size);
    scratch::pte::draw_cell(static_cast<unsigned>(cp), x, y, config.font_size,
                           cell_width * character_width(static_cast<unsigned>(cp)), config.color);
}
// Store screen contents separately from history. Native lists survive a new
// green flag, allowing init() to erase the previous viewport without pen_clear.
int painted(int row, int col) {
    int value; const int index = row * config.columns + col + 1;
    asm volatile("data_itemoflist LIST=\"__scl_console::painted\" INDEX=%1" : "=r"(value) : "r"(index) : "memory");
    return value;
}
void set_painted(int row, int col, int value) {
    const int index = row * config.columns + col + 1;
    asm volatile("data_replaceitemoflist LIST=\"__scl_console::painted\" INDEX=%0 ITEM=%1" : : "r"(index), "r"(value) : "memory");
}
int visible_cell(int row, int col) {
    const int logical = view + row;
    if (logical < oldest || logical > newest) return 0;
    return read_cell(logical, col);
}
void erase_region(const Options& layout, int row, int left, int right) {
    const int width = (layout.font_size + 1) / 2;
    const int margin = (layout.font_size + 31) / 32;
    const int x0 = 2*(layout.x + left * width)+1;
    const int x1 = 2*(layout.x + (right + 1) * width)-1;
    const int y0 = 2*(layout.y - row * row_height(layout.font_size) + margin)-1;
    const int y1 = 2*(layout.y - (row+1) * row_height(layout.font_size) + margin)+1;
    scratch::triangle::draw_fixed(x0, y0, x1, y0, x1, y1, 0xffffff, 2);
    scratch::triangle::draw_fixed(x0, y0, x1, y1, x0, y1, 0xffffff, 2);
}
void prepare_surface() {
    int count;
    asm volatile("data_lengthoflist LIST=\"__scl_console::paint_layout\"" : "=r"(count) : : "memory");
    if (count == 5) {
        int values[5];
        for (int i=0;i<5;++i) {
            const int index=i+1;
            asm volatile("data_itemoflist LIST=\"__scl_console::paint_layout\" INDEX=%1" : "=r"(values[i]) : "r"(index) : "memory");
        }
        painted_config.columns=clamp(values[0],2,120); painted_config.rows=clamp(values[1],1,80);
        painted_config.font_size=clamp(values[2],4,128); painted_config.x=values[3]; painted_config.y=values[4];
        for(int row=0;row<painted_config.rows;++row) {
            erase_region(painted_config,row,0,painted_config.columns-1);
            timing_refresh();
        }
    }
    const int cells=config.columns*config.rows;
    asm volatile("data_deletealloflist LIST=\"__scl_console::painted\";"
        "control_repeat TIMES=%0 SUBSTACK %{ data_addtolist LIST=\"__scl_console::painted\" ITEM=0; %};"
        "data_deletealloflist LIST=\"__scl_console::paint_layout\";"
        "data_addtolist LIST=\"__scl_console::paint_layout\" ITEM=%1;"
        "data_addtolist LIST=\"__scl_console::paint_layout\" ITEM=%2;"
        "data_addtolist LIST=\"__scl_console::paint_layout\" ITEM=%3;"
        "data_addtolist LIST=\"__scl_console::paint_layout\" ITEM=%4;"
        "data_addtolist LIST=\"__scl_console::paint_layout\" ITEM=%5;"
        : : "r"(cells), "r"(config.columns), "r"(config.rows), "r"(config.font_size), "r"(config.x), "r"(config.y) : "memory");
    // Establish the opaque white console background without clearing the pen layer.
    for(int row=0;row<config.rows;++row) {
        erase_region(config,row,0,config.columns-1);
        timing_refresh();
    }
    cursor_drawn=false;reset_surface=false;
}
void update_region(int row, int col, bool force=false) {
    if (row<0 || row>=config.rows || col<0 || col>=config.columns) return;
    if (!force && painted(row,col)==visible_cell(row,col)) return;
    int left=col,right=col;
    while(left>0 && (painted(row,left)==-1 || visible_cell(row,left)==-1)) --left;
    while(right+1<config.columns && (painted(row,right+1)==-1 || visible_cell(row,right+1)==-1)) ++right;
    bool erase=force;
    for(int c=left;c<=right;++c) if(painted(row,c)!=0) erase=true;
    if(cursor_drawn && cursor_screen_row==row && cursor_screen_col>=left && cursor_screen_col<=right) {
        erase=true;cursor_drawn=false;
    }
    // No timing_refresh between the two background triangles and glyph repair.
    if(erase) erase_region(config,row,left,right);
    for(int c=left;c<=right;++c) { draw(view+row,c);set_painted(row,c,visible_cell(row,c)); }
}
void ensure_init() { if (!initialized) init(); }
} // namespace

void init(const Options& options) {
    config = options;
    config.columns = clamp(config.columns, 2, 120);
    config.rows = clamp(config.rows, 1, 80);
    config.font_size = clamp(config.font_size, 4, 128);
    config.history_lines = clamp(config.history_lines, config.rows, 100000 / config.columns);
    config.color &= 0xffffff;
    initialized = true;
    turbowarp_input = scratch::is_turbowarp();
    oldest = newest = view = column = 0;
    utf_value = utf_min = 0;
    utf_remaining = 0;
    input_cancelled = input_cancelled || accepting_input;
    accepting_input = completed_input = false;
    input_closed = input_cancelled = false;
    cursor_visible = false;
    reset_surface = true;
    input_text().clear();
    completed_text().clear();
    repaint = true;
    scratch::events::init();
    if (!wheel_handler) wheel_handler = scratch::events::on_wheel(wheel_scroll, nullptr);
    if (!key_handler) key_handler = scratch::events::on_key(key_input, nullptr);
    const int cells = config.history_lines * config.columns;
    asm volatile(
        "data_deletealloflist LIST=\"__scl_console::cells\";"
        "control_repeat TIMES=%0 SUBSTACK %{ data_addtolist LIST=\"__scl_console::cells\" ITEM=0; %};"
        "looks_hide; pen_penUp;"
        : : "r"(cells) : "memory");
    clear_dirty();
}
void write(const char* bytes, std::size_t size) {
    ensure_init();
    if (accepting_input) cancel_input();
    for (std::size_t i = 0; i < size; ++i) byte(static_cast<unsigned char>(bytes[i]));
}
void write(const std::string& text) { write(text.data(), text.size()); }
void write(const char* text) {
    ensure_init();
    if (accepting_input) cancel_input();
    while (*text) byte(static_cast<unsigned char>(*text++));
}
void writeln(const std::string& text) { write(text); byte('\n'); }
void writeln(const char* text) { write(text); byte('\n'); }
void clear() {
    ensure_init();
    oldest = newest = view = column = 0;
    utf_remaining = 0;
    input_cancelled = input_cancelled || accepting_input;
    accepting_input = completed_input = false;
    input_text().clear();
    completed_text().clear();
    clear_row(0);
    clear_dirty();
    repaint = true;
}
void scroll(int lines) {
    ensure_init();
    // Clamp before subtraction to avoid overflow for arbitrary public inputs.
    lines = clamp(lines, -config.history_lines, config.history_lines);
    const int next = clamp(view - lines, oldest, bottom_view());
    if (next != view) { view = next; repaint = true; }
}
void flush() {
    ensure_init();
    if (flushing) return;
    flushing = true;
    scratch::events::poll();
    if (accepting_input && cursor_due()) {
        cursor_visible = !cursor_visible;
        reset_cursor_clock();
    }
    snapshot_position();
    if(reset_surface) prepare_surface();
    const bool show_cursor = accepting_input && cursor_visible && newest >= view && newest < view + config.rows;
    const int cursor_row = newest-view;
    const int cursor_col = column < config.columns ? column : config.columns-1;
    if(cursor_drawn && (!show_cursor || cursor_row!=cursor_screen_row || cursor_col!=cursor_screen_col)) {
        update_region(cursor_screen_row,cursor_screen_col,true);
    }
    if (repaint) {
        for(int row=0;row<config.rows;++row) for(int col=0;col<config.columns;++col) {
            update_region(row,col);
            timing_refresh();
        }
    } else {
        int count;
        asm volatile("data_lengthoflist LIST=\"__scl_console::dirty_rows\"" : "=r"(count) : : "memory");
        for(int i=1;i<=count;++i) {
            int row,col;
            asm volatile("data_itemoflist LIST=\"__scl_console::dirty_rows\" INDEX=%1" : "=r"(row) : "r"(i) : "memory");
            asm volatile("data_itemoflist LIST=\"__scl_console::dirty_cols\" INDEX=%1" : "=r"(col) : "r"(i) : "memory");
            update_region(row-view,col);
            timing_refresh();
        }
    }
    if(show_cursor && !cursor_drawn) {
        const int width=(config.font_size+1)/2;
        scratch::pte::draw_cell('_',config.x+cursor_col*width,config.y-cursor_row*row_height(config.font_size),config.font_size,width,config.color);
    }
    cursor_screen_row=cursor_row;cursor_screen_col=cursor_col;
    cursor_drawn = show_cursor;
    repaint = false;
    clear_dirty();
    timing_refresh();
    restore_position();
    flushing = false;
}
double days_since_2000() { return scratch::events::days_since_2000(); }

bool begin_input(const std::string& prompt, std::size_t max_bytes) {
    // Reject callback reentry: after Enter, subsequent keys in this same poll
    // batch must not accidentally feed a newly opened input session.
    if (scratch::events::is_dispatching()) return false;
    ensure_init();
    if (input_closed || accepting_input || completed_input || !key_handler) return false;
    input_cancelled = false;
    write(prompt);
    if (utf_remaining) { utf_remaining = 0; put(0xfffd); }
    if (column == config.columns) next_line();
    if (view != bottom_view()) { view = bottom_view(); repaint = true; }
    input_row = newest;
    input_column = column;
    const std::size_t retained_capacity = static_cast<std::size_t>(config.history_lines * config.columns - column);
    input_limit = max_bytes < retained_capacity ? max_bytes : retained_capacity;
    if (input_limit > 4096) input_limit = 4096;
    input_text().clear();
    input_text().reserve(input_limit);
    accepting_input = true;
    reset_cursor();
    return true;
}
bool try_read_line(std::string& text) {
    ensure_init();
    if (!completed_input) return false;
    text = std::move(completed_text());
    completed_input = false;
    return true;
}
std::string read_line(const std::string& prompt, std::size_t max_bytes) {
    std::string result;
    (void)read_line_result(result, prompt, max_bytes);
    return result;
}
InputStatus read_line_result(std::string& result, const std::string& prompt, std::size_t max_bytes) {
    ensure_init();
    if (input_closed) return InputStatus::eof;
    if (!begin_input(prompt, max_bytes)) return InputStatus::busy;
    while (accepting_input) {
        flush();
        if (accepting_input) scratch::events::yield();
    }
    return try_read_line_result(result);
}
void cancel_input() {
    ensure_init();
    if (!accepting_input) return;
    accepting_input = false;
    input_cancelled = true;
    input_text().clear();
    // Keep what was already echoed, and give the next output its own line.
    put('\n');
}
bool input_active() { ensure_init(); return accepting_input; }
InputStatus input_status() {
    ensure_init();
    if (completed_input) return InputStatus::line;
    if (input_closed) return InputStatus::eof;
    if (input_cancelled) return InputStatus::cancelled;
    return accepting_input ? InputStatus::pending : InputStatus::idle;
}
InputStatus try_read_line_result(std::string& text) {
    const auto status = input_status();
    if (status == InputStatus::line) try_read_line(text);
    if (status == InputStatus::cancelled) input_cancelled = false;
    return status;
}
void close_input() {
    ensure_init(); cancel_input(); input_cancelled = false; input_closed = true;
}
void reopen_input() { ensure_init(); input_closed = input_cancelled = false; }

int line_count() { ensure_init(); return newest - oldest + 1; }
int first_visible_line() { ensure_init(); return view - oldest; }
int cursor_column() { ensure_init(); return column; }
unsigned cell(int history_line, int col) {
    ensure_init();
    if (history_line < 0 || history_line >= line_count() || col < 0 || col >= config.columns) return 0;
    const int value = read_cell(oldest + history_line, col);
    return value > 0 ? static_cast<unsigned>(value) : 0;
}
} // namespace scratch::console
