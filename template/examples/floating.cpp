#include "scratch.hpp"

// Copy this file to src/main.cpp to try floating-point arithmetic.
int main() {
    volatile double input = 2.5; // Keep a real runtime calculation even at O2.
    double distance = input * 1.5 + 0.25;

    scratch::clear();
    scratch::pen_up();
    scratch::go_to(0, 0);
    scratch::pen_down();
    // The current Scratch assembly bridge accepts integer arguments.
    scratch::move(static_cast<int>(distance));
    scratch::pen_up();
    return distance == 4.0 ? 0 : 1;
}
