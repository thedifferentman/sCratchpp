#include <deque>

int main() {
    std::deque<int> d{2, 3};
    d.push_front(1); d.push_back(4); d.pop_front();
    if (d.size() != 3 || d.front() != 2 || d.back() != 4) return 1;
    return 0;
}
