#ifndef SCRATCH_STDIO_H
#define SCRATCH_STDIO_H
/* ABI declarations required by libc++ headers and string.cpp. I/O and numeric
 * formatting are a later runtime stage; reachable calls remain unresolved. */
#include <stddef.h>
#include <stdarg.h>
#define EOF (-1)
#ifdef __cplusplus
extern "C" {
#endif
typedef struct __scratch_FILE FILE;
typedef long fpos_t;
extern FILE *stdin, *stdout, *stderr;
int remove(const char *);
int snprintf(char *, size_t, const char *, ...);
int vsnprintf(char *, size_t, const char *, va_list);
#ifdef __cplusplus
}
#endif
#endif
