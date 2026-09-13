#include "scratch.hpp"
#include <algorithm>
#include <vector>

int main() {
    scratch::clear();
    scratch::pen_up();
    scratch::go_to(0, 0);
    scratch::point_in_direction(90);
    scratch::pen_size(2);
    scratch::pen_down();

    std::vector<int> lengths;
    for (int length = 160; length >= 4; length -= 4) lengths.push_back(length);
    std::sort(lengths.begin(), lengths.end());
    for (int length : lengths) {
        scratch::move(length);
        scratch::turn_right(90);
    }

    scratch::pen_up();
    return 0;
}