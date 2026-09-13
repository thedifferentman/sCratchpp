#ifndef SCLR_SOFTFLOAT_PLATFORM_H
#define SCLR_SOFTFLOAT_PLATFORM_H

#if !defined(__SIZEOF_POINTER__) || __SIZEOF_POINTER__ != 8
#error "The initial Scratch float runtime requires 64-bit pointers"
#endif
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "The initial Scratch float runtime requires a little-endian target"
#endif

/* Portable C only: do not include opts-GCC.h or enable host intrinsics. */
#define LITTLEENDIAN 1
#define SOFTFLOAT_FAST_INT64 1
#define SOFTFLOAT_ROUND_ODD 1
#define INLINE_LEVEL 0
#define INLINE static inline

#endif
