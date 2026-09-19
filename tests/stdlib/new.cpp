#include <new>
#include <cstdint>
#include <cstdlib>

struct alignas(64) Aligned { int value = 7; };
static int destroyed;
struct Counted { ~Counted() { ++destroyed; } };

int main() {
    void* first = ::operator new(1);
    void* second = ::operator new(1);
    if (reinterpret_cast<std::uintptr_t>(second) % __STDCPP_DEFAULT_NEW_ALIGNMENT__) return 7;
    ::operator delete(first); ::operator delete(second);
    auto value = new int(42);
    if (*value != 42) return 1;
    delete value;
    auto array = new Counted[3];
    delete[] array;
    if (destroyed != 3) return 2;
    auto aligned = new Aligned;
    if (aligned->value != 7 || reinterpret_cast<std::uintptr_t>(aligned) % 64) return 3;
    delete aligned;
    auto aligned_array = new (std::nothrow) Aligned[2];
    if (!aligned_array || aligned_array[1].value != 7 || reinterpret_cast<std::uintptr_t>(aligned_array) % 64) return 4;
    delete[] aligned_array;
    alignas(int) unsigned char storage[sizeof(int)];
    auto placed = new (storage) int(19);
    if (*placed != 19 || reinterpret_cast<void*>(placed) != storage) return 5;
    volatile std::size_t huge = static_cast<std::size_t>(-1);
    if (::operator new(huge, std::nothrow) != nullptr) return 6;
    auto sized = ::operator new(16);
    ::operator delete(sized, std::size_t(16));
    auto sized_aligned = ::operator new(64, std::align_val_t(64));
    ::operator delete(sized_aligned, std::size_t(64), std::align_val_t(64));
    return 0;
}
