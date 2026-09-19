// libc++ ABI surface implemented without exceptions, RTTI or host OS services.
#include <new>
#include <exception>
#include <__verbose_abort>
#include <stdlib.h>

namespace {
std::new_handler allocation_failure_handler;
std::terminate_handler termination_handler;
void *allocate(size_t size, size_t align, bool fatal) {
    for (;;) {
        void *p = nullptr;
        if (align) (void)posix_memalign(&p, align, size ? size : 1);
        else p = malloc(size ? size : 1);
        if (p) return p;
        auto handler = allocation_failure_handler;
        if (!handler) { if (fatal) std::terminate(); return nullptr; }
        handler();
    }
}
}
_LIBCPP_BEGIN_UNVERSIONED_NAMESPACE_STD
exception::~exception() noexcept = default;
const char *exception::what() const noexcept { return "std::exception"; }
bad_exception::~bad_exception() noexcept = default;
const char *bad_exception::what() const noexcept { return "std::bad_exception"; }
bad_alloc::bad_alloc() noexcept = default;
bad_alloc::~bad_alloc() noexcept = default;
const char *bad_alloc::what() const noexcept { return "std::bad_alloc"; }
bad_array_new_length::bad_array_new_length() noexcept = default;
bad_array_new_length::~bad_array_new_length() noexcept = default;
const char *bad_array_new_length::what() const noexcept { return "std::bad_array_new_length"; }
const nothrow_t nothrow{};
new_handler set_new_handler(new_handler handler) noexcept {
    auto old = allocation_failure_handler; allocation_failure_handler = handler; return old;
}
new_handler get_new_handler() noexcept { return allocation_failure_handler; }
terminate_handler set_terminate(terminate_handler handler) noexcept {
    auto old = get_terminate(); termination_handler = handler; return old;
}
terminate_handler get_terminate() noexcept { return termination_handler ? termination_handler : &abort; }
[[noreturn]] void terminate() noexcept { get_terminate()(); abort(); }
[[noreturn]] void __throw_bad_alloc() { terminate(); }
bool uncaught_exception() noexcept { return false; }
int uncaught_exceptions() noexcept { return 0; }
_LIBCPP_END_UNVERSIONED_NAMESPACE_STD

_LIBCPP_BEGIN_NAMESPACE_STD
[[noreturn]] void __throw_runtime_error(const char *) { std::terminate(); }
[[noreturn]] void __libcpp_verbose_abort(const char *, ...) noexcept { std::terminate(); }
_LIBCPP_END_NAMESPACE_STD

// Weak definitions preserve the standard replaceable allocation interface.
#define REPLACEABLE __attribute__((weak))
extern "C" void *__scratch_default_new(size_t size) { return allocate(size, 0, true); }
extern "C" void *__scratch_default_array_new(size_t size) { return ::operator new(size); }
extern "C" void *__scratch_default_aligned_new(size_t size, std::align_val_t align) { return allocate(size, static_cast<size_t>(align), true); }
extern "C" void *__scratch_default_aligned_array_new(size_t size, std::align_val_t align) { return ::operator new(size, align); }
REPLACEABLE void *operator new(size_t) __attribute__((alias("__scratch_default_new")));
REPLACEABLE void *operator new[](size_t) __attribute__((alias("__scratch_default_array_new")));
REPLACEABLE void *operator new(size_t, std::align_val_t) __attribute__((alias("__scratch_default_aligned_new")));
REPLACEABLE void *operator new[](size_t, std::align_val_t) __attribute__((alias("__scratch_default_aligned_array_new")));
// Without unwinding, a nothrow wrapper cannot recover from a replacement's
// allocation failure. Require matching nothrow replacements, as libc++ does.
REPLACEABLE void *operator new(size_t size, const std::nothrow_t &) noexcept {
    if (static_cast<void *(*)(size_t)>(&::operator new) != &__scratch_default_new) std::terminate();
    return allocate(size, 0, false);
}
REPLACEABLE void *operator new[](size_t size, const std::nothrow_t &) noexcept {
    if (static_cast<void *(*)(size_t)>(&::operator new[]) != &__scratch_default_array_new ||
        static_cast<void *(*)(size_t)>(&::operator new) != &__scratch_default_new) std::terminate();
    return allocate(size, 0, false);
}
REPLACEABLE void *operator new(size_t size, std::align_val_t align, const std::nothrow_t &) noexcept {
    if (static_cast<void *(*)(size_t, std::align_val_t)>(&::operator new) != &__scratch_default_aligned_new) std::terminate();
    return allocate(size, static_cast<size_t>(align), false);
}
REPLACEABLE void *operator new[](size_t size, std::align_val_t align, const std::nothrow_t &) noexcept {
    if (static_cast<void *(*)(size_t, std::align_val_t)>(&::operator new[]) != &__scratch_default_aligned_array_new ||
        static_cast<void *(*)(size_t, std::align_val_t)>(&::operator new) != &__scratch_default_aligned_new) std::terminate();
    return allocate(size, static_cast<size_t>(align), false);
}
REPLACEABLE void operator delete(void *p) noexcept { free(p); }
REPLACEABLE void operator delete[](void *p) noexcept { ::operator delete(p); }
REPLACEABLE void operator delete(void *p, size_t) noexcept { ::operator delete(p); }
REPLACEABLE void operator delete[](void *p, size_t) noexcept { ::operator delete[](p); }
REPLACEABLE void operator delete(void *p, const std::nothrow_t &) noexcept { ::operator delete(p); }
REPLACEABLE void operator delete[](void *p, const std::nothrow_t &) noexcept { ::operator delete[](p); }
REPLACEABLE void operator delete(void *p, std::align_val_t) noexcept { free(p); }
REPLACEABLE void operator delete[](void *p, std::align_val_t align) noexcept { ::operator delete(p, align); }
REPLACEABLE void operator delete(void *p, size_t, std::align_val_t align) noexcept { ::operator delete(p, align); }
REPLACEABLE void operator delete[](void *p, size_t, std::align_val_t align) noexcept { ::operator delete[](p, align); }
REPLACEABLE void operator delete(void *p, std::align_val_t align, const std::nothrow_t &) noexcept { ::operator delete(p, align); }
REPLACEABLE void operator delete[](void *p, std::align_val_t align, const std::nothrow_t &) noexcept { ::operator delete[](p, align); }
#undef REPLACEABLE
