/* Deliberately no include guard: assert changes with NDEBUG. */
#undef assert
#ifdef __cplusplus
extern "C" [[noreturn]] void __scratch_assert_fail(const char *, const char *, unsigned, const char *);
#else
_Noreturn void __scratch_assert_fail(const char *, const char *, unsigned, const char *);
#endif
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) ((expression) ? (void)0 : __scratch_assert_fail(#expression, __FILE__, __LINE__, __func__))
#endif
#if !defined(__cplusplus)
#define static_assert _Static_assert
#endif
