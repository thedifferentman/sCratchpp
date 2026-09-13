#include <cstdlib>
#include <cassert>
#include <new>
#include <vector>
#include <optional>

int main() {
#if MODE == 1
    std::abort();
#elif MODE == 2
    volatile bool condition = false;
    assert(condition);
#elif MODE == 3
    volatile std::size_t huge = static_cast<std::size_t>(-1);
    auto p = ::operator new(huge);
    __asm__ volatile("data_setvariableto VARIABLE=\"unexpected_allocation\" VALUE=%0"
                     : : "r"(static_cast<int>(p != nullptr)) : "memory");
#elif MODE == 4
    std::vector<int> values{1};
    return values.at(3);
#elif MODE == 5
    std::optional<int> value;
    return value.value();
#endif
    return 99;
}
