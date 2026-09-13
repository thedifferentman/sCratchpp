"""Deterministic native differential tests for the integer-only wrappers.

Finite arithmetic and conversions use a separately implemented exact rational
oracle, not host float arithmetic. Native math is used for sqrt and special
value classification only. This test does not replace Scratch VM integration.
"""
from __future__ import annotations

import argparse
import ctypes
from fractions import Fraction
import math
from pathlib import Path
import random
import struct


def decode(bits: int, width: int) -> float:
    return struct.unpack("<f" if width == 32 else "<d", bits.to_bytes(width // 8, "little"))[0]


def encode(value: float, width: int) -> int:
    # Some host packing paths truncate a signaling-NaN payload to zero while
    # narrowing. A NaN oracle must not accidentally turn that into infinity.
    if math.isnan(value):
        return 0x7fc00000 if width == 32 else 0x7ff8000000000000
    try:
        packed = struct.pack("<f" if width == 32 else "<d", value)
    except OverflowError:
        packed = struct.pack("<f" if width == 32 else "<d", math.copysign(math.inf, value))
    return int.from_bytes(packed, "little")


def nearest_even(numerator: int, denominator: int) -> int:
    q, r = divmod(numerator, denominator)
    return q + (2 * r > denominator or (2 * r == denominator and q % 2 != 0))


def exact_bits(value: Fraction, width: int, negative_zero: bool = False) -> int:
    precision, bias = (24, 127) if width == 32 else (53, 1023)
    sign = int(value < 0 or (not value and negative_zero)) << (width - 1)
    if not value:
        return sign
    value = abs(value)
    n, d = value.numerator, value.denominator
    exponent = n.bit_length() - d.bit_length()
    if (n < d << exponent) if exponent >= 0 else (n << -exponent < d):
        exponent -= 1
    minimum = 1 - bias
    exponent = max(exponent, minimum)
    shift = precision - 1 - exponent
    significand = nearest_even(n << shift, d) if shift >= 0 else nearest_even(n, d << -shift)
    if significand >= 1 << precision:
        significand >>= 1
        exponent += 1
    if exponent > bias:
        return sign | (((1 << (width - precision)) - 1) << (precision - 1))
    hidden = 1 << (precision - 1)
    if significand < hidden:
        return sign | significand
    return sign | ((exponent + bias) << (precision - 1)) | (significand - hidden)


def is_nan_bits(bits: int, width: int) -> bool:
    fraction = 23 if width == 32 else 52
    exponent_mask = 0x7f800000 if width == 32 else 0x7ff0000000000000
    return bits & exponent_mask == exponent_mask and bits & ((1 << fraction) - 1) != 0


def exact_fma_bits(a: int, b: int, c: int, width: int) -> int:
    """Independent FMA oracle, including special values, without math.fma.

    Python 3.12 and earlier do not expose math.fma. Finite results use exact
    rationals with a single final rounding; special cases follow IEEE rules
    directly rather than relying on a particular host libm's availability.
    """
    sign = 1 << (width - 1)
    infinity = 0x7f800000 if width == 32 else 0x7ff0000000000000
    nan = infinity | (1 << (22 if width == 32 else 51))
    if any(is_nan_bits(value, width) for value in (a, b, c)):
        return nan
    a_magnitude, b_magnitude, c_magnitude = (value & (sign - 1) for value in (a, b, c))
    product_sign = (a ^ b) & sign
    if a_magnitude == infinity or b_magnitude == infinity:
        if a_magnitude == 0 or b_magnitude == 0:
            return nan
        if c_magnitude == infinity and (c & sign) != product_sign:
            return nan
        return product_sign | infinity
    if c_magnitude == infinity:
        return c
    result = Fraction(decode(a, width)) * Fraction(decode(b, width)) + Fraction(decode(c, width))
    negative_zero = (a_magnitude == 0 or b_magnitude == 0) and c_magnitude == 0 and bool(product_sign & c)
    return exact_bits(result, width, negative_zero)


def signed(value: int, width: int) -> int:
    return value - (1 << width) if value >> (width - 1) else value


def exact_integral_bits(bits: int, width: int, mode: str) -> int:
    """Round the exact rational value, retaining the input sign of zero."""
    sign = bits >> (width - 1)
    if is_nan_bits(bits, width):
        return bits | (1 << (22 if width == 32 else 51))
    value = decode(bits, width)
    if not math.isfinite(value):
        return bits
    value = Fraction(value)
    if mode == "ceil":
        integer = -(-value.numerator // value.denominator)
    elif mode == "floor":
        integer = value.numerator // value.denominator
    else:
        n, d = abs(value.numerator), value.denominator
        q, r = divmod(n, d)
        if mode == "round":
            q += 2 * r >= d
        elif mode == "roundeven":
            q += 2 * r > d or (2 * r == d and q % 2 != 0)
        elif mode != "trunc":
            raise ValueError(mode)
        integer = -q if sign else q
    return exact_bits(Fraction(integer), width, bool(sign))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    parser.add_argument("--samples", type=int, default=1500)
    args = parser.parse_args()
    library = ctypes.CDLL(str(args.library.resolve()))
    count = 0
    rng = random.Random(0x53434c52)

    def function(name, output, *inputs):
        fn = getattr(library, "__sclr_" + name)
        fn.restype = ctypes.c_uint32 if output == 32 else ctypes.c_uint64
        fn.argtypes = [ctypes.c_uint32 if width == 32 else ctypes.c_uint64 for width in inputs]
        return fn

    def check(actual, expected, width, context, *, nan_equivalent=True):
        nonlocal count
        count += 1
        if actual != expected and not (nan_equivalent and is_nan_bits(actual, width) and is_nan_bits(expected, width)):
            raise AssertionError(f"{context}: expected 0x{expected:0{width // 4}x}, got 0x{actual:0{width // 4}x}")

    # Ensure the independent oracle handles ties, subnormals and overflow.
    assert exact_bits(Fraction(1) + Fraction(1, 1 << 24), 32) == 0x3f800000
    assert exact_bits(Fraction(1) + Fraction(3, 1 << 24), 32) == 0x3f800002
    assert exact_bits(Fraction(1, 1 << 149), 32) == 1
    assert exact_bits(Fraction(1, 1 << 1074), 64) == 1

    for width in (32, 64):
        sign = 1 << (width - 1)
        infinity = 0x7f800000 if width == 32 else 0x7ff0000000000000
        special = [0, sign, 1, sign | 1, infinity - 1, sign | (infinity - 1),
                   infinity, sign | infinity, infinity | 1, infinity | (1 << (22 if width == 32 else 51)),
                   encode(1.0, width), encode(-1.0, width), encode(2.0, width), encode(3.0, width)]
        ops = {name: function(f"f{width}_{name}", width, width, width)
               for name in ("add", "sub", "mul", "div", "rem")}
        compare = {name: function(f"f{width}_{name}", 32, width, width) for name in ("eq", "lt", "le")}
        isnan = function(f"f{width}_isnan", 32, width)
        sqrt = function(f"f{width}_sqrt", width, width)
        fma = function(f"f{width}_fma", width, width, width, width)
        rounding_rng = random.Random(0x524f554e44 + width)
        rounding_inputs = special + [rounding_rng.getrandbits(width) for _ in range(args.samples)]
        # Probe adjacent bit patterns around halves, integral boundaries,
        # and the point where all representable values are already integral.
        precision = 24 if width == 32 else 53
        for edge in (0.5, 1.0, 1.5, 2.5, 3.5, 2 ** (precision - 1), 2 ** precision):
            encoded = encode(edge, width)
            for delta in (-1, 0, 1):
                rounding_inputs.extend((encoded + delta, sign | (encoded + delta)))
        for mode in ("ceil", "floor", "trunc", "round", "roundeven"):
            rounding = function(f"f{width}_{mode}", width, width)
            for a in rounding_inputs:
                # Unlike arithmetic's NaN-equivalence test, verify that these
                # operations explicitly quiet sNaNs and preserve their payload.
                check(rounding(a), exact_integral_bits(a, width, mode), width,
                      f"f{width} {mode} {a:x}", nan_equivalent=False)
        pairs = [(a, b) for a in special for b in special]
        pairs += [(rng.getrandbits(width), rng.getrandbits(width)) for _ in range(args.samples)]
        for a, b in pairs:
            av, bv = decode(a, width), decode(b, width)
            check(isnan(a), int(math.isnan(av)), 32, f"f{width} isnan {a:x}")
            for op, expected in (("eq", av == bv), ("lt", av < bv), ("le", av <= bv)):
                check(compare[op](a, b), int(expected), 32, f"f{width} {op} {a:x} {b:x}")
            for op in ops:
                finite = math.isfinite(av) and math.isfinite(bv)
                if finite and (op not in ("div", "rem") or bv != 0):
                    x, y = Fraction(av), Fraction(bv)
                    negzero = False
                    if op == "add":
                        result = x + y
                        negzero = a == sign and b == sign
                    elif op == "sub":
                        result = x - y
                        negzero = a == sign and b == 0
                    elif op == "mul":
                        result = x * y
                        negzero = bool((a ^ b) & sign)
                    elif op == "div":
                        result = x / y
                        negzero = bool((a ^ b) & sign)
                    else:
                        quotient = x / y
                        truncated = abs(quotient.numerator) // quotient.denominator
                        if quotient < 0:
                            truncated = -truncated
                        result = x - truncated * y
                        negzero = bool(a & sign)
                    expected = exact_bits(result, width, negzero)
                else:
                    if op == "add":
                        result = av + bv
                    elif op == "sub":
                        result = av - bv
                    elif op == "mul":
                        result = av * bv
                    elif op == "div":
                        if bv == 0:
                            result = math.nan if av == 0 or math.isnan(av) else math.copysign(math.inf, -1 if (a ^ b) & sign else 1)
                        else:
                            result = av / bv
                    else:
                        try:
                            result = math.fmod(av, bv)
                        except ValueError:
                            result = math.nan
                    expected = encode(result, width)
                check(ops[op](a, b), expected, width, f"f{width} {op} {a:x} {b:x}")
            if not math.isnan(av):
                root = math.sqrt(av) if av >= 0 else math.nan
                check(sqrt(a), encode(root, width), width, f"f{width} sqrt {a:x}")
            c = rng.getrandbits(width)
            cv = decode(c, width)
            if math.isfinite(av) and math.isfinite(bv) and math.isfinite(cv):
                check(fma(a, b, c), exact_fma_bits(a, b, c, width), width,
                      f"f{width} fma {a:x} {b:x} {c:x}")

        for int_width in (32, 64):
            mask = (1 << int_width) - 1
            integers = [0, 1, mask, mask >> 1, 1 << (int_width - 1)]
            integers += [rng.getrandbits(int_width) for _ in range(args.samples)]
            for unsigned in (False, True):
                prefix = "u" if unsigned else "i"
                to_fp = function(f"{prefix}{int_width}_to_f{width}", width, int_width)
                to_int = function(f"f{width}_to_{prefix}{int_width}", int_width, width)
                for integer in integers:
                    value = integer if unsigned else signed(integer, int_width)
                    expected = exact_bits(Fraction(value), width)
                    check(to_fp(integer), expected, width, f"{prefix}{int_width} to f{width} {value}")
                lower = 0 if unsigned else -(1 << (int_width - 1))
                upper = (1 << int_width) if unsigned else (1 << (int_width - 1))
                candidates = [a for a, _ in pairs]
                candidates += [encode(x, width) for x in (-2.8, -1.1, -0.5, 0.5, 1.1, 2.8)]
                # Probe both sides of every representability boundary, plus
                # adjacent FP encodings rather than only decimal samples.
                for edge in (lower, upper - 1, upper, -1, 0, 1):
                    encoding = exact_bits(Fraction(edge), width)
                    candidates.extend((encoding + delta) & ((1 << width) - 1) for delta in (-2, -1, 0, 1, 2))
                for a in candidates:
                    av = decode(a, width)
                    if math.isfinite(av) and lower <= math.trunc(av) < upper:
                        check(to_int(a), math.trunc(av) & mask, int_width,
                              f"f{width} to {prefix}{int_width} {a:x}", nan_equivalent=False)
                    else:
                        # These inputs are LLVM poison for ordinary fptosi/
                        # fptoui. Check the chosen deterministic SoftFloat
                        # concrete result without advertising saturation.
                        sentinel = mask if unsigned else 1 << (int_width - 1)
                        check(to_int(a), sentinel, int_width,
                              f"invalid f{width} to {prefix}{int_width} {a:x}", nan_equivalent=False)
        other = 96 - width
        convert = function(f"f{width}_to_f{other}", other, width)
        for a, _ in pairs:
            av = decode(a, width)
            expected = (exact_bits(Fraction(av), other, bool(a & sign))
                        if math.isfinite(av) else encode(av, other))
            check(convert(a), expected, other, f"f{width} to f{other} {a:x}")

        fma_specials = ((infinity, 0, 0), (0, infinity, 0),
                        (infinity, encode(1.0, width), sign | infinity),
                        (infinity, encode(1.0, width), infinity),
                        (sign, encode(1.0, width), sign),
                        (sign, encode(1.0, width), 0),
                        (infinity | 1, encode(1.0, width), 0))
        for a, b, c in fma_specials:
            check(fma(a, b, c), exact_fma_bits(a, b, c, width), width,
                  f"f{width} special fma {a:x} {b:x} {c:x}")

    # FMA's single rounding must survive translation; these are nonzero even
    # though separately rounded multiplication followed by addition yields zero.
    check(function("f32_fma", 32, 32, 32, 32)(0x3f800001, 0x3f7ffffe, 0xbf800000),
          exact_bits(-Fraction(1, 1 << 46), 32), 32, "f32 fused cancellation")
    check(function("f64_fma", 64, 64, 64, 64)(0x3ff0000000000001, 0x3feffffffffffffe, 0xbff0000000000000),
          exact_bits(-Fraction(1, 1 << 104), 64), 64, "f64 fused cancellation")
    print(f"PASS: {count} deterministic wrapper checks (exact rational arithmetic/conversion oracle)")


if __name__ == "__main__":
    main()
