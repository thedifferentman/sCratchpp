#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <string>

int main() {
    std::map<int, int> m{{3, 30}, {1, 10}, {2, 20}};
    m[4] = 40;
    m.erase(2);
    if (m.size() != 3 || m.begin()->first != 1 || m.find(3)->second != 30) return 1;
    std::set<int> s{5, 1, 5, 3};
    if (s.size() != 3 || *s.lower_bound(2) != 3) return 2;
    std::unordered_map<std::string, int> u;
    u["alpha"] = 4; u["beta"] = 9; u["alpha"] += 2;
    u.reserve(8);
    if (u.size() != 2 || u.find("alpha")->second != 6) return 3;
    u.erase("beta");
    if (u.count("beta") || u.size() != 1) return 4;
    std::unordered_set<int> us{8, 8, 2};
    if (us.size() != 2 || !us.count(2)) return 5;
    return 0;
}
