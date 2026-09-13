#ifndef SCLR_FLOAT_RUNTIME_H
#define SCLR_FLOAT_RUNTIME_H

#include <stdint.h>

/* Every floating-point operand/result is an IEEE binary interchange bit pattern.
 * No declaration uses a C floating-point type. Comparisons return exactly 0/1.
 */
#if defined(SCLR_TEST_EXPORT) && defined(_WIN32)
#define SCLR_FLOAT_API __declspec(dllexport)
#else
#define SCLR_FLOAT_API
#endif

#define SCLR_DECLARE_FLOAT(BITS, UINT) \
SCLR_FLOAT_API UINT __sclr_f##BITS##_add(UINT, UINT); \
SCLR_FLOAT_API UINT __sclr_f##BITS##_sub(UINT, UINT); \
SCLR_FLOAT_API UINT __sclr_f##BITS##_mul(UINT, UINT); \
SCLR_FLOAT_API UINT __sclr_f##BITS##_div(UINT, UINT); \
SCLR_FLOAT_API UINT __sclr_f##BITS##_rem(UINT, UINT); \
SCLR_FLOAT_API UINT __sclr_f##BITS##_sqrt(UINT); \
SCLR_FLOAT_API UINT __sclr_f##BITS##_ceil(UINT); \
SCLR_FLOAT_API UINT __sclr_f##BITS##_floor(UINT); \
SCLR_FLOAT_API UINT __sclr_f##BITS##_trunc(UINT); \
SCLR_FLOAT_API UINT __sclr_f##BITS##_round(UINT); \
SCLR_FLOAT_API UINT __sclr_f##BITS##_roundeven(UINT); \
SCLR_FLOAT_API UINT __sclr_f##BITS##_fma(UINT, UINT, UINT); \
SCLR_FLOAT_API uint32_t __sclr_f##BITS##_eq(UINT, UINT); \
SCLR_FLOAT_API uint32_t __sclr_f##BITS##_lt(UINT, UINT); \
SCLR_FLOAT_API uint32_t __sclr_f##BITS##_le(UINT, UINT); \
SCLR_FLOAT_API uint32_t __sclr_f##BITS##_isnan(UINT); \
SCLR_FLOAT_API UINT __sclr_i32_to_f##BITS(uint32_t); \
SCLR_FLOAT_API UINT __sclr_u32_to_f##BITS(uint32_t); \
SCLR_FLOAT_API UINT __sclr_i64_to_f##BITS(uint64_t); \
SCLR_FLOAT_API UINT __sclr_u64_to_f##BITS(uint64_t); \
SCLR_FLOAT_API uint32_t __sclr_f##BITS##_to_i32(UINT); \
SCLR_FLOAT_API uint32_t __sclr_f##BITS##_to_u32(UINT); \
SCLR_FLOAT_API uint64_t __sclr_f##BITS##_to_i64(UINT); \
SCLR_FLOAT_API uint64_t __sclr_f##BITS##_to_u64(UINT);

SCLR_DECLARE_FLOAT(32, uint32_t)
SCLR_DECLARE_FLOAT(64, uint64_t)
#undef SCLR_DECLARE_FLOAT

SCLR_FLOAT_API uint64_t __sclr_f32_to_f64(uint32_t);
SCLR_FLOAT_API uint32_t __sclr_f64_to_f32(uint64_t);

#endif
