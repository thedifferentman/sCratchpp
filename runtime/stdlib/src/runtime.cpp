// Freestanding, single-execution-context C/C++ support for Scratch byte memory.
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifndef SCRATCH_HEAP_BYTES
#define SCRATCH_HEAP_BYTES 16384
#endif
#ifndef SCRATCH_ATEXIT_CAPACITY
#define SCRATCH_ATEXIT_CAPACITY 128
#endif
namespace {
constexpr size_t alignment = alignof(max_align_t);
struct alignas(max_align_t) Block { size_t span, requested, payload; bool allocated; };
static_assert(SCRATCH_HEAP_BYTES >= 128 && SCRATCH_HEAP_BYTES % alignment == 0,
              "SCRATCH_HEAP_BYTES must be aligned and at least 128");
alignas(max_align_t) unsigned char heap[SCRATCH_HEAP_BYTES];
bool initialized;
int error_number;
bool power_two(size_t n) { return n && !(n & (n - 1)); }
size_t rounded(size_t n) { return (n + alignment - 1) & ~(alignment - 1); }
Block *at(size_t offset) { return reinterpret_cast<Block *>(heap + offset); }
void initialize() {
    if (!initialized) {
        *at(0) = {sizeof(heap), 0, 0, false};
        initialized = true;
    }
}
void coalesce() {
    for (size_t offset = 0; offset < sizeof(heap);) {
        Block *b = at(offset);
        while (!b->allocated && offset + b->span < sizeof(heap) &&
               !at(offset + b->span)->allocated)
            b->span += at(offset + b->span)->span;
        offset += b->span;
    }
}
void split(Block *b, size_t span) {
    if (b->span - span >= sizeof(Block) + alignment) {
        Block *next = reinterpret_cast<Block *>(reinterpret_cast<unsigned char *>(b) + span);
        *next = {b->span - span, 0, 0, false};
        b->span = span;
    }
}
void *allocate(size_t size, size_t align) {
    initialize();
    if (!size) size = 1;
    if (size > sizeof(heap) || align > sizeof(heap)) { errno = ENOMEM; return nullptr; }
    for (size_t offset = 0; offset < sizeof(heap);) {
        Block *b = at(offset);
        if (!b->allocated) {
            uintptr_t start = reinterpret_cast<uintptr_t>(b) + sizeof(Block);
            size_t payload = ((start + align - 1) & ~(align - 1)) - reinterpret_cast<uintptr_t>(b);
            if (payload <= b->span && size <= b->span - payload) {
                size_t span = rounded(payload + size);
                split(b, span);
                b->allocated = true; b->requested = size; b->payload = payload;
                return reinterpret_cast<unsigned char *>(b) + payload;
            }
        }
        offset += b->span;
    }
    errno = ENOMEM;
    return nullptr;
}
Block *find(void *p) {
    if (!initialized) return nullptr;
    for (size_t offset = 0; offset < sizeof(heap);) {
        Block *b = at(offset);
        if (b->allocated && reinterpret_cast<unsigned char *>(b) + b->payload == p) return b;
        offset += b->span;
    }
    return nullptr;
}
struct Destructor { void (*function)(void *); void *argument; void *dso; void (*plain)(); bool active; };
Destructor destructors[SCRATCH_ATEXIT_CAPACITY];
size_t destructor_count;
void (*quick_handlers[SCRATCH_ATEXIT_CAPACITY])();
size_t quick_count;
}
extern "C" {
int *__scratch_errno_location() { return &error_number; }
void *malloc(size_t size) { return allocate(size, alignment); }
void free(void *p) {
    if (!p) return;
    Block *b = find(p);
    if (!b) abort(); // Invalid/double free is undefined in C; fail deterministically here.
    b->allocated = false;
    coalesce();
}
void *calloc(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size) { errno = ENOMEM; return nullptr; }
    size_t bytes = count * size;
    void *p = malloc(bytes);
    if (p) memset(p, 0, bytes);
    return p;
}
void *realloc(void *p, size_t size) {
    if (!p) return malloc(size);
    if (!size) { free(p); return nullptr; }
    Block *b = find(p);
    if (!b) abort();
    if (size > sizeof(heap)) { errno = ENOMEM; return nullptr; }
    size_t old_size = b->requested;
    size_t offset = reinterpret_cast<unsigned char *>(b) - heap;
    if (size > b->span - b->payload && offset + b->span < sizeof(heap)) {
        Block *next = at(offset + b->span);
        if (!next->allocated && size <= b->span + next->span - b->payload) b->span += next->span;
    }
    if (size <= b->span - b->payload) {
        split(b, rounded(b->payload + size));
        b->requested = size;
        coalesce();
        return p;
    }
    void *replacement = malloc(size);
    if (!replacement) return nullptr;
    memcpy(replacement, p, old_size);
    free(p);
    return replacement;
}
void *aligned_alloc(size_t align, size_t size) {
    if (!power_two(align) || size % align) { errno = EINVAL; return nullptr; }
    return allocate(size, align < alignment ? alignment : align);
}
int posix_memalign(void **result, size_t align, size_t size) {
    if (!power_two(align) || align < sizeof(void *)) return EINVAL;
    int saved = errno;
    void *p = allocate(size, align < alignment ? alignment : align);
    errno = saved;
    if (!p) return ENOMEM;
    *result = p;
    return 0;
}
void *memcpy(void *destination, const void *source, size_t n) {
    auto *d = static_cast<unsigned char *>(destination);
    auto *s = static_cast<const unsigned char *>(source);
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return destination;
}
void *memmove(void *destination, const void *source, size_t n) {
    auto *d = static_cast<unsigned char *>(destination);
    auto *s = static_cast<const unsigned char *>(source);
    if (reinterpret_cast<uintptr_t>(d) < reinterpret_cast<uintptr_t>(s)) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else {
        while (n) { --n; d[n] = s[n]; }
    }
    return destination;
}
void *memset(void *destination, int value, size_t n) {
    auto *d = static_cast<unsigned char *>(destination);
    for (size_t i = 0; i < n; ++i) d[i] = static_cast<unsigned char>(value);
    return destination;
}
int memcmp(const void *a, const void *b, size_t n) {
    auto *x = static_cast<const unsigned char *>(a), *y = static_cast<const unsigned char *>(b);
    for (size_t i = 0; i < n; ++i) if (x[i] != y[i]) return int(x[i]) - int(y[i]);
    return 0;
}
void *memchr(const void *source, int value, size_t n) {
    auto *s = static_cast<const unsigned char *>(source);
    for (size_t i = 0; i < n; ++i) if (s[i] == static_cast<unsigned char>(value)) return const_cast<unsigned char *>(s+i);
    return nullptr;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) ++n; return n; }
size_t strnlen(const char *s, size_t max) { size_t n = 0; while (n < max && s[n]) ++n; return n; }
char *strcpy(char *d, const char *s) { char *result = d; do { *d++ = *s; } while (*s++); return result; }
char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0; for (; i < n && s[i]; ++i) d[i] = s[i];
    for (; i < n; ++i) d[i] = 0;
    return d;
}
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
char *strncat(char *d, const char *s, size_t n) {
    size_t end = strlen(d), i = 0;
    for (; i < n && s[i]; ++i) d[end+i] = s[i];
    d[end+i] = 0;
    return d;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}
int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return static_cast<unsigned char>(a[i]) - static_cast<unsigned char>(b[i]);
        if (!a[i]) break;
    }
    return 0;
}
char *strchr(const char *s, int c) { do { if (*s == static_cast<char>(c)) return const_cast<char *>(s); } while (*s++); return nullptr; }
char *strrchr(const char *s, int c) { const char *found = nullptr; do { if (*s == static_cast<char>(c)) found = s; } while (*s++); return const_cast<char *>(found); }
char *strstr(const char *s, const char *needle) {
    if (!*needle) return const_cast<char *>(s);
    for (; *s; ++s) { size_t i = 0; while (needle[i] && s[i] == needle[i]) ++i; if (!needle[i]) return const_cast<char *>(s); }
    return nullptr;
}
size_t strspn(const char *s, const char *set) { size_t n = 0; while (s[n] && strchr(set, s[n])) ++n; return n; }
size_t strcspn(const char *s, const char *set) { size_t n = 0; while (s[n] && !strchr(set, s[n])) ++n; return n; }
char *strpbrk(const char *s, const char *set) { size_t n = strcspn(s, set); return s[n] ? const_cast<char *>(s+n) : nullptr; }
char *strtok(char *s, const char *set) {
    static char *next;
    if (!s) s = next;
    if (!s) return nullptr;
    s += strspn(s, set);
    if (!*s) { next = nullptr; return nullptr; }
    char *end = s + strcspn(s, set);
    if (*end) { *end = 0; next = end+1; } else next = nullptr;
    return s;
}
char *strerror(int error) {
    // Static mutable buffers match the C signature; callers must not modify them.
    static char no_memory[] = "Out of memory", invalid[] = "Invalid argument", unknown[] = "Unknown error";
    return error == ENOMEM ? no_memory : error == EINVAL ? invalid : unknown;
}
int strcoll(const char *a, const char *b) { return strcmp(a, b); }
size_t strxfrm(char *d, const char *s, size_t n) {
    size_t length = strlen(s);
    if (n) { size_t copied = length < n ? length : n; memcpy(d, s, copied); if (length < n) d[length] = 0; }
    return length;
}
int abs(int n) { return n < 0 ? -n : n; }
long labs(long n) { return n < 0 ? -n : n; }
long long llabs(long long n) { return n < 0 ? -n : n; }
div_t div(int a, int b) { return {a/b, a%b}; }
ldiv_t ldiv(long a, long b) { return {a/b, a%b}; }
lldiv_t lldiv(long long a, long long b) { return {a/b, a%b}; }
void *bsearch(const void *key, const void *base, size_t count, size_t size, int (*compare)(const void *, const void *)) {
    auto *start = static_cast<const unsigned char *>(base);
    while (count) {
        size_t half = count/2;
        auto *item = start + half*size;
        int result = compare(key, item);
        if (!result) return const_cast<unsigned char *>(item);
        if (result > 0) { start = item + size; count -= half+1; } else count = half;
    }
    return nullptr;
}
void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *)) {
    // In-place heapsort: O(n log n), bounded stack and no heap allocations.
    if (!size || count < 2) return;
    auto *bytes = static_cast<unsigned char *>(base);
    auto swap = [&](size_t a, size_t b) { for (size_t j=0; j<size; ++j) { unsigned char t=bytes[a*size+j]; bytes[a*size+j]=bytes[b*size+j]; bytes[b*size+j]=t; } };
    auto sift = [&](size_t root, size_t end) {
        while (root < end/2) {
            size_t child = root*2+1;
            if (child+1 < end && compare(bytes+child*size, bytes+(child+1)*size) < 0) ++child;
            if (compare(bytes+root*size, bytes+child*size) >= 0) break;
            swap(root, child); root=child;
        }
    };
    for (size_t start=count/2; start;) { --start; sift(start, count); }
    for (size_t end=count; end>1;) { --end; swap(0,end); sift(0,end); }
}
int __cxa_atexit(void (*function)(void *), void *argument, void *dso) {
    // Compact only inactive tail entries, preserving registration order.
    while (destructor_count && !destructors[destructor_count-1].active) --destructor_count;
    if (destructor_count == SCRATCH_ATEXIT_CAPACITY) return -1;
    destructors[destructor_count++] = {function, argument, dso, nullptr, true};
    return 0;
}
void __cxa_finalize(void *dso) {
    for (;;) {
        size_t i=destructor_count;
        while (i && (!destructors[i-1].active || (dso && destructors[i-1].dso != dso))) --i;
        if (!i) break;
        Destructor entry = destructors[i-1];
        destructors[i-1].active = false;
        if (entry.plain) entry.plain(); else entry.function(entry.argument);
    }
}
int atexit(void (*function)()) {
    int result = __cxa_atexit(nullptr, nullptr, nullptr);
    if (!result) destructors[destructor_count-1].plain = function;
    return result;
}
int at_quick_exit(void (*function)()) {
    if (quick_count == SCRATCH_ATEXIT_CAPACITY) return -1;
    quick_handlers[quick_count++] = function;
    return 0;
}
void *__dso_handle = &__dso_handle;
int __cxa_guard_acquire(uint64_t *guard) {
    auto *bytes = reinterpret_cast<unsigned char *>(guard);
    if (bytes[0]) return 0;
    if (bytes[1]) abort();
    bytes[1] = 1;
    return 1;
}
void __cxa_guard_release(uint64_t *guard) { auto *b=reinterpret_cast<unsigned char *>(guard); b[0]=1; b[1]=0; }
void __cxa_guard_abort(uint64_t *guard) { reinterpret_cast<unsigned char *>(guard)[1]=0; }
[[noreturn]] void __cxa_pure_virtual() { abort(); }
[[noreturn]] void __cxa_deleted_virtual() { abort(); }
[[noreturn]] void abort() { __builtin_trap(); }
[[noreturn]] void __scratch_assert_fail(const char *, const char *, unsigned, const char *) { abort(); }
[[noreturn]] void _Exit(int code) {
    unsigned value = static_cast<unsigned>(code);
    __asm__ volatile(
        "data_setvariableto VARIABLE=\"exit_code\" VALUE=(operator_mod NUM1=%0 NUM2=4294967296);"
        "data_deletealloflist LIST=\"return_bytes\";"
        "data_addtolist LIST=\"return_bytes\" ITEM=%1;"
        "data_addtolist LIST=\"return_bytes\" ITEM=%2;"
        "data_addtolist LIST=\"return_bytes\" ITEM=%3;"
        "data_addtolist LIST=\"return_bytes\" ITEM=%4;"
        "data_setvariableto VARIABLE=\"__scl_status\" VALUE=\"done\";"
        "control_stop STOP_OPTION=\"all\""
        : : "r"(code), "r"(value & 255u), "r"((value >> 8) & 255u),
            "r"((value >> 16) & 255u), "r"(value >> 24) : "memory");
    __builtin_unreachable();
}
[[noreturn]] void exit(int code) { __cxa_finalize(nullptr); _Exit(code); }
[[noreturn]] void quick_exit(int code) { while (quick_count) quick_handlers[--quick_count](); _Exit(code); }
}
__attribute__((destructor)) static void scratch_finalize() { __cxa_finalize(nullptr); }
