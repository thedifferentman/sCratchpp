#include "platform.h"
#include "softfloat.h"
#include "runtime.h"

_Static_assert(_Alignof(uint8_t) == 1 && _Alignof(uint16_t) == 2 &&
               _Alignof(uint32_t) == 4 && _Alignof(uint64_t) == 8,
               "Unsupported integer ABI alignment in float runtime");

/* Normal LLVM floating-point operations use round-to-nearest, ties-to-even.
 * This private linked runtime retains SoftFloat's default rounding mode and
 * after-rounding tininess detection. Constrained FP/environment access must
 * not be mapped to these entry points without a separate environment design.
 */

#define SCLR_DEFINE_FLOAT(BITS, UINT, SIGN, INFINITY) \
UINT __sclr_f##BITS##_add(UINT a, UINT b) { \
    return f##BITS##_add((float##BITS##_t){a}, (float##BITS##_t){b}).v; \
} \
UINT __sclr_f##BITS##_sub(UINT a, UINT b) { \
    return f##BITS##_sub((float##BITS##_t){a}, (float##BITS##_t){b}).v; \
} \
UINT __sclr_f##BITS##_mul(UINT a, UINT b) { \
    return f##BITS##_mul((float##BITS##_t){a}, (float##BITS##_t){b}).v; \
} \
UINT __sclr_f##BITS##_div(UINT a, UINT b) { \
    return f##BITS##_div((float##BITS##_t){a}, (float##BITS##_t){b}).v; \
} \
UINT __sclr_f##BITS##_sqrt(UINT a) { \
    return f##BITS##_sqrt((float##BITS##_t){a}).v; \
} \
UINT __sclr_f##BITS##_ceil(UINT a) { \
    return f##BITS##_roundToInt((float##BITS##_t){a}, softfloat_round_max, false).v; \
} \
UINT __sclr_f##BITS##_floor(UINT a) { \
    return f##BITS##_roundToInt((float##BITS##_t){a}, softfloat_round_min, false).v; \
} \
UINT __sclr_f##BITS##_trunc(UINT a) { \
    return f##BITS##_roundToInt((float##BITS##_t){a}, softfloat_round_minMag, false).v; \
} \
UINT __sclr_f##BITS##_round(UINT a) { \
    return f##BITS##_roundToInt((float##BITS##_t){a}, softfloat_round_near_maxMag, false).v; \
} \
UINT __sclr_f##BITS##_roundeven(UINT a) { \
    return f##BITS##_roundToInt((float##BITS##_t){a}, softfloat_round_near_even, false).v; \
} \
UINT __sclr_f##BITS##_fma(UINT a, UINT b, UINT c) { \
    return f##BITS##_mulAdd((float##BITS##_t){a}, (float##BITS##_t){b}, \
                           (float##BITS##_t){c}).v; \
} \
uint32_t __sclr_f##BITS##_isnan(UINT a) { \
    return (a & ~(SIGN)) > (INFINITY); \
} \
uint32_t __sclr_f##BITS##_eq(UINT a, UINT b) { \
    return f##BITS##_eq((float##BITS##_t){a}, (float##BITS##_t){b}); \
} \
uint32_t __sclr_f##BITS##_lt(UINT a, UINT b) { \
    return f##BITS##_lt_quiet((float##BITS##_t){a}, (float##BITS##_t){b}); \
} \
uint32_t __sclr_f##BITS##_le(UINT a, UINT b) { \
    return f##BITS##_le_quiet((float##BITS##_t){a}, (float##BITS##_t){b}); \
} \
UINT __sclr_f##BITS##_rem(UINT a, UINT b) { \
    UINT r = f##BITS##_rem((float##BITS##_t){a}, (float##BITS##_t){b}).v; \
    /* IEEE remainder chooses the nearest integer quotient. LLVM frem uses \
     * the quotient truncated toward zero. A finite, nonzero remainder with \
     * the opposite sign is corrected by adding b with a's sign. This sum \
     * is exactly representable; no host division or FP operation is used. */ \
    if ((r & ~(SIGN)) == 0) return a & (SIGN); \
    if ((r & ~(SIGN)) > (INFINITY)) return r; \
    if ((r ^ a) & (SIGN)) \
        r = f##BITS##_add((float##BITS##_t){r}, \
                          (float##BITS##_t){(b & ~(SIGN)) | (a & (SIGN))}).v; \
    return r; \
} \
UINT __sclr_i32_to_f##BITS(uint32_t a) { \
    return i32_to_f##BITS((int32_t)a).v; \
} \
UINT __sclr_u32_to_f##BITS(uint32_t a) { \
    return ui32_to_f##BITS(a).v; \
} \
UINT __sclr_i64_to_f##BITS(uint64_t a) { \
    return i64_to_f##BITS((int64_t)a).v; \
} \
UINT __sclr_u64_to_f##BITS(uint64_t a) { \
    return ui64_to_f##BITS(a).v; \
} \
uint32_t __sclr_f##BITS##_to_i32(UINT a) { \
    return (uint32_t)f##BITS##_to_i32_r_minMag((float##BITS##_t){a}, false); \
} \
uint32_t __sclr_f##BITS##_to_u32(UINT a) { \
    return (uint32_t)f##BITS##_to_ui32_r_minMag((float##BITS##_t){a}, false); \
} \
uint64_t __sclr_f##BITS##_to_i64(UINT a) { \
    return (uint64_t)f##BITS##_to_i64_r_minMag((float##BITS##_t){a}, false); \
} \
uint64_t __sclr_f##BITS##_to_u64(UINT a) { \
    return (uint64_t)f##BITS##_to_ui64_r_minMag((float##BITS##_t){a}, false); \
}

SCLR_DEFINE_FLOAT(32, uint32_t, UINT32_C(0x80000000), UINT32_C(0x7f800000))
SCLR_DEFINE_FLOAT(64, uint64_t, UINT64_C(0x8000000000000000),
                  UINT64_C(0x7ff0000000000000))
#undef SCLR_DEFINE_FLOAT

uint64_t __sclr_f32_to_f64(uint32_t a) {
    return f32_to_f64((float32_t){a}).v;
}

uint32_t __sclr_f64_to_f32(uint64_t a) {
    return f64_to_f32((float64_t){a}).v;
}
