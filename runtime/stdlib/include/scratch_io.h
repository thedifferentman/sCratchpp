#ifndef SCRATCH_IO_H
#define SCRATCH_IO_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Terminal providers return byte counts, 0 for EOF, or -1 with errno on error.
 * Read/write implementations must be supplied by a linked terminal package.
 * Flush has a no-op weak default so programs without stdio need no terminal. */
int __scrpp_io_read(unsigned char*,size_t);
int __scrpp_io_write(int,const unsigned char*,size_t);
int __scrpp_io_flush(int);
#ifdef __cplusplus
}
#endif
#endif
