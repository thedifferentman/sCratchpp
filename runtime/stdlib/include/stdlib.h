#ifndef SCRATCH_STDLIB_H
#define SCRATCH_STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 2147483647
#define MB_CUR_MAX 1
#ifdef __cplusplus
extern "C" {
#define SCRATCH_NORETURN [[noreturn]]
#else
#define SCRATCH_NORETURN _Noreturn
#endif
typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
typedef struct { long long quot, rem; } lldiv_t;
void *malloc(size_t);
void free(void *);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void *aligned_alloc(size_t, size_t);
int posix_memalign(void **, size_t, size_t);
SCRATCH_NORETURN void abort(void);
SCRATCH_NORETURN void exit(int);
SCRATCH_NORETURN void _Exit(int);
SCRATCH_NORETURN void quick_exit(int);
int atexit(void (*)(void));
int at_quick_exit(void (*)(void));
int abs(int);
long labs(long);
long long llabs(long long);
div_t div(int, int);
ldiv_t ldiv(long, long);
lldiv_t lldiv(long long, long long);
void *bsearch(const void *, const void *, size_t, size_t, int (*)(const void *, const void *));
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
/* Conversion and environment entry points are declared for libc++ headers.
   Their implementation is outside the first-batch runtime. */
int atoi(const char *);
long atol(const char *);
long long atoll(const char *);
double atof(const char *);
long strtol(const char *, char **, int);
long long strtoll(const char *, char **, int);
unsigned long strtoul(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int);
float strtof(const char *, char **);
double strtod(const char *, char **);
long double strtold(const char *, char **);
int rand(void);
void srand(unsigned);
char *getenv(const char *);
int system(const char *);
int mblen(const char *, size_t);
int mbtowc(wchar_t *, const char *, size_t);
int wctomb(char *, wchar_t);
size_t mbstowcs(wchar_t *, const char *, size_t);
size_t wcstombs(char *, const wchar_t *, size_t);
#ifdef __cplusplus
}
#endif
#undef SCRATCH_NORETURN
#endif
