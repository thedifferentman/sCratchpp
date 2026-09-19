#pragma once

#include <cstddef>
#include <string>

namespace scratch::console {

// One terminal owns an opaque white viewport on the Program pen layer. It uses native Scratch
// lists for bounded history, leaving the C++ heap available to the program.
struct Options {
    int columns = 40;
    int rows = 14;
    int font_size = 20;
    int history_lines = 128;
    int x = -230;
    int y = 170;
    unsigned color = 0x202020;
};

void init(const Options& options = Options{});
void write(const char* bytes, std::size_t size);
void write(const std::string& text);
void write(const char* text);
void writeln(const std::string& text);
void writeln(const char* text = "");
// Render pending changes and process queued wheel input. Every ~8ms of this
// work, yield at a character boundary. Call repeatedly in an interactive loop.
void flush();
// Reset text/history; occupied cells are erased by the next flush().
void clear();
// Positive lines moves toward older history; negative lines toward the end.
void scroll(int lines);

// Single-line ASCII keyboard input. TurboWarp uses Shift for letter case, uses
// Backspace to erase, and accepts literal backslashes. Vanilla Scratch folds
// letters to upper case and uses backslash as erase. Enter submits on both.
// Consume a completed line before beginning another; flush() services input.
bool begin_input(const std::string& prompt = "", std::size_t max_bytes = 1024);
bool try_read_line(std::string& text);
// Blocking convenience; returns empty without starting when invoked inside an
// event callback or when another input/completed line already occupies input.
std::string read_line(const std::string& prompt = "", std::size_t max_bytes = 1024);
void cancel_input();
bool input_active();

// Explicit completion states for stream adapters; an empty Line is not EOF.
enum class InputStatus { idle, pending, line, cancelled, eof, busy };
InputStatus input_status();
InputStatus try_read_line_result(std::string& text);
InputStatus read_line_result(std::string& text, const std::string& prompt = "", std::size_t max_bytes = 1024);
// EOF is persistent until reopen_input()/init(); unfinished editing is discarded.
void close_input();
void reopen_input();

// Convenience alias of events::days_since_2000(), retaining fractional days.
double days_since_2000();

// Small state API useful to an embedding application; indices are zero based.
int line_count();
int first_visible_line();
int cursor_column();
unsigned cell(int history_line, int column);

} // namespace scratch::console
