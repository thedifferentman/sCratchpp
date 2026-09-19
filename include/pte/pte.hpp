#pragma once

namespace scratch::pte {
// Natural advance in 1/64 normalized-height units. Missing codepoints use square.
int measure_units(unsigned codepoint);

// Draw one glyph at a top-left origin using native Scratch arithmetic.
// No yielding/reentry is permitted within a glyph. Leaves the pen raised.
// Position, visibility and the other pen settings are owned by the caller.
void draw(unsigned codepoint, int x, int y, int size, unsigned rgb);

// Fit the natural advance into a cell, reducing horizontal scale if needed,
// and center it in that cell. Vertical scale/pen size still use height.
// A nonpositive height or cell width produces no drawing.
void draw_cell(unsigned codepoint, int x, int y, int height, int cell_width, unsigned rgb);
}
