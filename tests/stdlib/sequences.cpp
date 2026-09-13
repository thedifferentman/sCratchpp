#include <array>
#include <vector>
#include <deque>
#include <list>
#include <string>
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
    std::deque<int> d{2, 3};
    d.push_front(1); d.push_back(4); d.pop_front();
    if (d.size() != 3 || d.front() != 2 || d.back() != 4) return 3;
    std::list<int> l{4, 1, 3, 1};
    l.sort(); l.unique();
    if (l.size() != 3 || l.front() != 1 || l.back() != 4) return 4;
    std::string text = "abcdefghijklmnopqrstuvwx";
    text += "yz";
    text.replace(2, 2, "--");
    if (text.size() != 26 || text.substr(0, 5) != "ab--e" || text.find("xyz") != 23) return 5;
    auto copy = text;
    text.clear();
    if (!text.empty() || copy.back() != 'z') return 6;
    return 0;
}
