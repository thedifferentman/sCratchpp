#include <new>
#include <cstdlib>

static int allocations, releases;
void* operator new(std::size_t n) {
    ++allocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    std::abort();
}
void operator delete(void* p) noexcept { ++releases; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
#if MODE == 1
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    ++allocations;
    return std::malloc(n ? n : 1);
}
#endif

int main() {
    // Explicit calls ensure allocation elision does not erase replacement checks.
    auto ordinary = ::operator new(8);
    ::operator delete(ordinary);
    auto optional = ::operator new(8, std::nothrow);
    if (!optional) return 1;
    ::operator delete(optional);
    return allocations == 2 && releases == 2 ? 0 : 2;
}
