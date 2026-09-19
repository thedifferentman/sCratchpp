// Copy this file to src/main.cpp to try the resource package in sCrpp.toml.
#include "scratch.hpp"

int main() {
    scratch::clear();
    scratch::pen_up();
    scratch::point_in_direction(90);
    scratch::set_costume("marker"); // example::marker
    scratch::show();
    for (int x = -120; x <= 120; x += 60) {
        scratch::go_to(x, 0);
        scratch::stamp();
    }
    scratch::hide(); // Existing stamps remain on the pen layer.
    return 0;
}
