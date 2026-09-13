"""Regenerate independent arbitrary-precision numeric reference fixtures.

Python integers supply the oracle; the C++ test executes Scratch AST using doubles.
No Python installation is required to consume the checked-in fixtures.
"""
import json
import random
from pathlib import Path

rng = random.Random(0x5C12A7C)
rows = []
for bits in (65, 73, 127, 128, 129, 191, 256, 257):
    mask = (1 << bits) - 1
    sign = 1 << (bits - 1)

    def raw(v):
        return list((v & mask).to_bytes((bits + 7) // 8, "little"))

    def signed(v):
        return v - (1 << bits) if v & sign else v

    for case in range(12):
        a = (0, 1, mask, sign, sign - 1)[case] if case < 5 else rng.getrandbits(bits)
        b = (mask, 1, mask - 1, sign - 1, sign)[case] if case < 5 else rng.getrandbits(bits)
        k = (0, 1, bits - 1, bits, mask)[case] if case < 5 else rng.randrange(bits)
        sa, sb = signed(a), signed(b)
        q = (abs(sa) // abs(sb)) * (-1 if (sa < 0) != (sb < 0) else 1)
        values = {
            "add": a + b, "sub": a - b, "mul": a * b,
            "and": a & b, "or": a | b, "xor": a ^ b,
            "udiv": a // b, "urem": a % b,
            "sdiv": q, "srem": sa - q * sb,
            "shl": a << k if k < bits else 0,
            "lshr": a >> k if k < bits else 0,
            "ashr": sa >> k if k < bits else 0,
        }
        rows.append({"bits": bits, "a": raw(a), "b": raw(b), "k": raw(k),
                     "expected": {op: raw(value) for op, value in values.items()}})

Path(__file__).with_name("wide_cases.json").write_text(json.dumps(rows, separators=(",", ":")) + "\n")
