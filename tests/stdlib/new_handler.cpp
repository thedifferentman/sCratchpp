#include <new>
#include <cstdlib>

#ifndef SCRATCH_TEST_HEAP_BYTES
#define SCRATCH_TEST_HEAP_BYTES 16384
#endif
static void* reserve;
static int calls;
static void release_reserve() {
    ++calls;
    std::free(reserve);
    reserve = nullptr;
    std::set_new_handler(nullptr);
}
static void decline() {
    ++calls;
    std::set_new_handler(nullptr);
}

int main() {
    reserve = std::malloc(SCRATCH_TEST_HEAP_BYTES - 1024);
    if (!reserve) return 1;
    if (std::set_new_handler(release_reserve) != nullptr || std::get_new_handler() != release_reserve) return 2;
    auto recovered = ::operator new(2048);
    if (!recovered || reserve || calls != 1 || std::get_new_handler()) return 3;
    ::operator delete(recovered);
    std::set_new_handler(decline);
    volatile std::size_t huge = static_cast<std::size_t>(-1);
    if (::operator new(huge, std::nothrow) != nullptr || calls != 2 || std::get_new_handler()) return 4;
    return 0;
}
