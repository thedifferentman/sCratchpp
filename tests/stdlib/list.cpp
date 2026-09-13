#include <list>
#include <forward_list>

int main() {
    std::list<int> l{4, 1, 3, 1};
    l.sort(); l.unique();
    if (l.size() != 3 || l.front() != 1 || l.back() != 4) return 1;
    std::forward_list<int> f{3, 2};
    f.push_front(1); f.reverse();
    if (f.front() != 2) return 2;
    f.pop_front();
    if (f.front() != 3) return 3;
    return 0;
}
