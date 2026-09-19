#pragma once
namespace scratch::triangle {
// C1 eight-layer filled triangle using native Scratch arithmetic. Coordinates
// are integers at the LLVM boundary; geometric intermediates retain fractions.
// No clear, hide, yield or event loop. Leaves pen up; position/pen style change.
// Collinear or coincident points (nonpositive computed area) produce no strokes.
void draw(int ax, int ay, int bx, int by, int cx, int cy, unsigned rgb);
// Fixed-point coordinates: units_per_pixel=2 accepts half-pixel positions.
void draw_fixed(int ax, int ay, int bx, int by, int cx, int cy, unsigned rgb, int units_per_pixel);
}
