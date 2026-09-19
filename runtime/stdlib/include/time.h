#ifndef SCRATCH_TIME_H
#define SCRATCH_TIME_H
/* Types needed by chrono/atomic transitive includes, not a clock runtime. */
#include <stddef.h>
#include <locale.h>
typedef long time_t;
typedef long clock_t;
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; };
struct timespec { time_t tv_sec; long tv_nsec; };
#define CLOCKS_PER_SEC 1000000L
#define TIME_UTC 1
#ifdef __cplusplus
extern "C" {
#endif
size_t strftime(char*,size_t,const char*,const struct tm*);
size_t strftime_l(char*,size_t,const char*,const struct tm*,locale_t);
#ifdef __cplusplus
}
#endif
#endif
