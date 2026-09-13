#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cerrno>

#define CHECK(test, code) do { if (!(test)) return code; } while (0)

int main() {
    char a[24], b[24];
    CHECK(std::memset(a, 'x', sizeof a) == a, 1);
    CHECK(std::memcpy(b, a, sizeof a) == b && std::memcmp(a, b, sizeof a) == 0, 2);
    std::strcpy(a, "abcde");
    std::memmove(a + 1, a, 6);
    CHECK(std::strcmp(a, "aabcde") == 0, 3);
    std::memmove(a, a + 1, 6);
    CHECK(std::strcmp(a, "abcde") == 0 && std::strlen(a) == 5, 4);
    std::strncpy(b, "q", 5);
    CHECK(b[0] == 'q' && b[1] == 0 && b[4] == 0, 5);
    std::strcat(b, "rs");
    CHECK(std::strcmp(b, "qrs") == 0 && std::strncmp(b, "qr!", 2) == 0, 6);
    CHECK(std::strchr(a, 'c') == a + 2 && std::strrchr("aba", 'a')[1] == 0, 7);
    CHECK(std::strstr(a, "cd") == a + 2 && std::memchr(a, 'd', 5) == a + 3, 8);
    const unsigned char high[] = {255}, low[] = {1};
    CHECK(std::memcmp(high, low, 1) > 0, 9);

    auto p = static_cast<unsigned char*>(std::malloc(13));
    CHECK(p && reinterpret_cast<std::uintptr_t>(p) % 16 == 0, 10);
    for (int i = 0; i < 13; ++i) p[i] = static_cast<unsigned char>(i + 2);
    auto grown = static_cast<unsigned char*>(std::realloc(p, 70));
    CHECK(grown, 11);
    for (int i = 0; i < 13; ++i) CHECK(grown[i] == i + 2, 12);
    auto shrunk = static_cast<unsigned char*>(std::realloc(grown, 7));
    CHECK(shrunk && shrunk[6] == 8, 13);
    volatile std::size_t huge = static_cast<std::size_t>(-1);
    errno = 0;
    CHECK(std::realloc(shrunk, huge) == nullptr && errno == ENOMEM && shrunk[6] == 8, 14);
    CHECK(std::realloc(shrunk, 0) == nullptr, 15);
    auto zeros = static_cast<unsigned char*>(std::calloc(7, 3));
    CHECK(zeros, 16);
    for (int i = 0; i < 21; ++i) CHECK(zeros[i] == 0, 17);
    std::free(zeros);
    errno = 0;
    CHECK(std::calloc(huge, 2) == nullptr && errno == ENOMEM, 18);
    auto aligned = std::aligned_alloc(64, 128);
    CHECK(aligned && reinterpret_cast<std::uintptr_t>(aligned) % 64 == 0, 19);
    std::free(aligned);
    errno = 0;
    CHECK(std::aligned_alloc(64, 63) == nullptr && errno == EINVAL, 20);
    void* posix = nullptr;
    CHECK(posix_memalign(&posix, 128, 5) == 0 && reinterpret_cast<std::uintptr_t>(posix) % 128 == 0, 21);
    std::free(posix);
    posix = reinterpret_cast<void*>(123);
    CHECK(posix_memalign(&posix, 3, 5) == EINVAL && posix == reinterpret_cast<void*>(123), 22);
    auto first = std::malloc(5000), second = std::malloc(5000);
    CHECK(first && second, 23);
    std::free(first); std::free(second);
    auto merged = std::malloc(10000);
    CHECK(merged, 24);
    std::free(merged); std::free(nullptr);
    return 0;
}
