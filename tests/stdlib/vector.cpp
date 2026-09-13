#include <array>
#include <vector>
#include <algorithm>
#include <numeric>

int main() {
    std::array<int, 4> initial{4, 1, 3, 2};
    std::vector<int> v(initial.begin(), initial.end());
    v.push_back(5);
    std::sort(v.begin(), v.end());
    if (std::accumulate(v.begin(), v.end(), 0) != 15 || v.front() != 1 || v.back() != 5) return 1;
    v.erase(v.begin() + 1);
    v.insert(v.begin(), 9);
    if (v.size() != 5 || v[0] != 9 || v[2] != 3) return 2;
    return 0;
}
