# LLVM integer-only floating-point runtime

`softfloat/` supplies exact IEEE binary32/binary64 operations for the Scratch
backend. It is compiled from portable C into ordinary LLVM integer operations,
then linked and lowered through the same byte-memory backend as user code.
There is no JavaScript, TurboWarp extension, native floating-point shortcut,
platform SDK, or native assembly in the generated runtime.

## Build

```sh
python tools/build_float_runtime.py
python tools/build_float_runtime.py --host-test
```

Clang is selected by `--clang`, then `SCRATCH_CLANG` / `CLANG`, then `clang-22`
or `clang` on `PATH`. An explicitly selected executable that cannot be found
fails immediately; the build does not silently switch toolchains or probe a
fixed Windows installation directory. Clang 22.1.8 has been tested.

The **guest** target defaults to `x86_64-unknown-linux-gnu` on every host. It
describes the Scratch module's memory ABI, not the operating system running the
compiler. Clang's own freestanding `stdint.h` and `stdbool.h` suffice to build
this bitcode; no target sysroot, target C/C++ standard library, or native linker
is required. Use `--target` only to deliberately select another guest target.

The output directory (default `build/runtime`) contains:

- `scratch-float.bc`: one linked runtime module, ready for the LLVM frontend.
- `scratch-float.ll`: readable equivalent IR for diagnosis.
- `scratch-float.json`: export list, source list, target, layout, compiler
  version, bitcode checksum and remaining LLVM intrinsic dependencies.
- `SoftFloat-LICENSE.txt`: unchanged upstream license, colocated with the runtime
  for installation and SB3 license packaging.
- `softfloat-amalgamation.c`: generated single C translation unit referencing
  unmodified vendored sources.

`--host-test` also builds a local native library from the same C source and runs
the deterministic wrapper tests. It queries the selected Clang with
`-dumpmachine` and preserves that compiler's **host** target. ELF hosts use a
shared PIC library, macOS uses a dynamic library, and Windows supports both the
MSVC ABI and the independent LLVM-MinGW GNU ABI. Linker options are selected
for that ABI: MinGW never receives MSVC `/noentry` flags. Windows native test
DLLs use LLD with no CRT or SDK; the guest bitcode target stays unchanged.
The GNU ABI test generates a minimal successful DLL loader callback in the
output directory instead of requiring CRT startup code. This support source is
used only for the native test and is never included in guest bitcode.

`--host-linker lld` selects a linker flavor; an executable path selects an exact
linker. `--host-sysroot /path/to/sysroot` optionally selects a native test
sysroot. These options never modify the guest ABI. The native test must target
the machine running Python so that `ctypes` can load the resulting library;
cross-generated guest bitcode does not have this requirement. Builds are
entirely offline; upstream C sources are already vendored.

## Layout and symbol integration

The initial runtime requires little-endian byte order, 64-bit pointers and ABI
alignments of 1/2/4/8 bytes for i8/i16/i32/i64. Struct size, field offset and
alignment must also agree. A matching layout with a different textual spelling
can be requested explicitly:

```sh
python tools/build_float_runtime.py --data-layout 'e-p:64:64-i64:64-n8:16:32:64-S128'
```

This option uses LLVM-C to verify relevant integer and aggregate layouts and
write the exact requested layout into bitcode. `--llvm-library` selects the
LLVM shared library, followed by `SCRATCH_LLVM_LIBRARY` / `LLVM_LIBRARY`.
Explicit selections must name an existing file and never fall back silently.
Automatic discovery searches Clang's `bin`, adjacent `lib` and `lib64`, and
then the system library loader. Supported names include `LLVM-C.dll`,
`libLLVM-22.so`, versioned ELF names such as `libLLVM-22.so.1`, and
`libLLVM.dylib`. The selected library must match the Clang/IR version and the
Python host architecture. CMake supplies the configured Clang and LLVM library
paths explicitly. Incompatible layouts fail the build. Merely replacing
arbitrary DataLayout strings in already compiled C IR is not supported.

All upstream global/function LLVM names are placed under `__sclr_sf_` to avoid
collisions with application symbols. Public bit-pattern entry points use
`__sclr_`. Both prefixes are reserved to the compiler. The backend should link
this module only if required and retain only reachable wrappers/helpers/data.
Helpers are normal compiled functions; they are not opaque VM native calls.

SoftFloat initialization globals are part of this module. In particular, normal
arithmetic uses round-to-nearest, ties-to-even, and detects tininess after
rounding. The host program must not overwrite the reserved state globals.

## Public ABI

The authoritative C declarations are in `softfloat/runtime.h`. There are 50
public entry points. All inputs and results are scalar `uint32_t` or `uint64_t`;
their LLVM signatures consequently contain only i32/i64, with no C `float` or
`double`. A signed integer is passed/returned as its two's-complement bit pattern.

| Entry points | Meaning |
| --- | --- |
| `__sclr_f32_add/sub/mul/div/rem`, `__sclr_f64_add/sub/mul/div/rem` | Binary arithmetic, returning an IEEE bit pattern |
| `__sclr_f32_sqrt`, `__sclr_f64_sqrt` | Correctly rounded square root |
| `__sclr_f32_{ceil,floor,trunc,round,roundeven}` and f64 counterparts | Exact integral rounding, preserving signed zero; `rint/nearbyint` use ties-even under the default LLVM FP environment |
| `__sclr_f32_fma`, `__sclr_f64_fma` | Three inputs; multiplication plus addition with one rounding |
| `__sclr_f32_eq/lt/le`, `__sclr_f64_eq/lt/le` | Ordered comparison; i32 result is exactly 0 or 1 |
| `__sclr_f32_isnan`, `__sclr_f64_isnan` | i32 0/1, for constructing all LLVM `fcmp` predicates |
| `__sclr_i32/u32/i64/u64_to_f32/f64` | Signed/unsigned integer to IEEE bit pattern |
| `__sclr_f32/f64_to_i32/u32/i64/u64` | Toward-zero conversion to integer bit pattern |
| `__sclr_f32_to_f64`, `__sclr_f64_to_f32` | Exact widening / correctly rounded narrowing |

`rem` implements LLVM `frem` / C `fmod`, **not** IEEE nearest-integer remainder.
SoftFloat's remainder result is adjusted by one signed divisor if its nonzero
sign differs from the dividend. The correction is exactly representable. Zero
results retain the dividend's sign; infinities and NaNs follow the appropriate
special-value paths. Huge quotients never pass through a host floating-point
division or integer conversion.

For integer conversion, finite values in range truncate toward zero. Invalid
LLVM `fptosi` / `fptoui` inputs yield poison; these wrappers choose SoftFloat's
deterministic concrete result (signed minimum or unsigned maximum) for such
inputs. This is **not** a saturating-conversion API. Conversions to narrower
LLVM integer types need the backend's own width handling; constrained FP and
observable exception environments require a separate interface.

`eq/lt/le` return false for unordered inputs. Positive and negative zero compare
equal. NaN payload selection follows SoftFloat's 8086-SSE specialization, which
contains portable C policy and does not require or emit SSE instructions.

## Typed IR lowering

`include/scratch/float_lower.hpp` exposes `floating_dependencies(module)` and
`lower_floating(module)`. The first reports exactly the wrapper roots needed
by supported operations; the second replaces operations with integer calls and
bitcasts in the normalized backend JSON. Newly generated SSA identifiers use
the `__fp` prefix and avoid every existing identifier.

Supported lowering covers scalar and fixed-vector `fadd/fsub/fmul/fdiv/frem`,
`fneg`, all sixteen `fcmp` predicates, signed/unsigned integer conversions,
`fptrunc/fpext`, and the `fma/fmuladd/sqrt/fabs/copysign` and six min/max
intrinsics. Comparisons preserve ordered/unordered behavior; min/max variants
preserve their NaN propagation rules and signed-zero ordering. `fneg/fabs` and
`copysign` use bit operations without any floating runtime dependency and
preserve NaN payload bits.

Integers narrower than 32/64 bits are appropriately sign-/zero-extended before
conversion; narrow integer results are truncated after the runtime conversion.
Widths above 64 and floating formats other than IEEE binary32/binary64 produce
contextual diagnostics rather than lossy approximations. Constrained FP,
floating atomic operations and floating vector reductions are separate
extensions; they are not implicitly implemented by this lowering pass.

The `tests/fixtures/float_*.ll` integration cases cover ordinary arithmetic,
fused cancellation, signed-zero/NaN min/max, fixed-vector conversions, every
comparison predicate and runtime-free sign-bit operations. Their expected
returns have also been checked using native Clang execution. The two negative
fixtures `negative/float_half.ll` and `negative/float_wide_integer.ll` establish
explicit unsupported-format and unsupported-width diagnostics.

## LLVM dependency audit

Every build rejects inline assembly, native floating-point arithmetic or
conversion instructions, and unresolved non-intrinsic declarations. With
Clang 22.1.8 `-O1`, the current module contains 125 functions before reachability
pruning and requires only:

```text
llvm.abs.i32 / llvm.abs.i64
llvm.fshl.i64
llvm.smax.i16
llvm.umax.i32 / llvm.umin.i32
llvm.usub.sat.i16
```

The exact set is recorded in `scratch-float.json`. Vectorization and loop
unrolling are disabled to limit generated code size. No llvm-link executable is
required because the selected sources form one translation unit.

## Verification and limits

The default native suite performs over 66,000 deterministic checks. Finite
add/subtract/multiply/divide/remainder/FMA and conversions use an independent
exact rational oracle with explicit ties-to-even encoding. Tests include normal
and subnormal inputs, halfway rounding, signed zero, infinities, signaling and
quiet NaNs, fused cancellation, integer conversion boundaries and neighboring
FP encodings. Square root is checked against host `sqrt`; special-value
classification also uses host math. Equivalent NaN payloads are accepted.
FMA uses the exact rational oracle and explicit IEEE special-value rules; it
does not require Python 3.13's `math.fma`, so Python 3.12 remains supported.

The suite validates this runtime and its wrappers. It does not replace end-to-end
execution of emitted Scratch blocks, prove exhaustive IEEE conformance, or
establish Scratch runtime performance. Those are backend integration checks.
Half precision, x87 extended, quad precision, transcendental functions,
decimal conversion and constrained floating-point environments are not exposed
by this initial wrapper ABI.

The portable tool-selection and link-argument tests are run with:

```sh
python -m unittest discover -s tests -p test_toolchain.py
python tests/varargs_native.py --clang /path/to/clang
```

The second command checks the AMD64 SysV fixtures against LLVM native execution;
it accepts the same host linker/sysroot options. It supports AMD64 Windows,
Linux, and macOS toolchain selection. A non-AMD64 native target is rejected with
an explanation because these fixtures embed the AMD64 SysV `va_list` layout;
this is a reference-test limitation, not a restriction on compiler host builds.
The native-reference bridge removes the fixed fixtures' `dso_local` markings
from its temporary IR copy so that Clang can select relocations suitable for a
shared library. This does not modify the original guest fixtures.

During the portability change, real Windows Clang 22.1.8/MSVC-ABI and independent
Clang 22/MinGW-GNU-ABI freestanding LLD tests passed all 66,344 runtime checks and
both native varargs references. Linux under WSL (Python 3.12, Clang 22, ELF/LLD)
also passed all 66,344 checks and both native varargs references. The unit tests
cover Windows GNU/MSVC, ELF, and macOS discovery and flag construction. macOS
has parameter-construction coverage only, not an executed host validation.

## Upstream source and license

The implementation is Berkeley SoftFloat **Release 3e**, dated **2018-01-20**,
by John R. Hauser. Source provenance and the official archive SHA-256 are pinned
in `softfloat/UPSTREAM.json`. Files under `softfloat/upstream/` retain their
original contents and notices. `softfloat/SOURCE_SHA256SUMS` records their hashes.

- [Official release and source archive](https://www.jhauser.us/arithmetic/SoftFloat.html)
- [Official interface documentation](https://www.jhauser.us/arithmetic/SoftFloat-3/doc/SoftFloat.html)
- [Official build documentation](https://www.jhauser.us/arithmetic/SoftFloat-3/doc/SoftFloat-source.html)
- Local license: `softfloat/upstream/COPYING.txt` (BSD 3-clause terms).

Source redistribution must retain the upstream notices. Distribution of an SB3
or other binary containing this runtime must reproduce the copyright notice,
conditions and disclaimer in accompanying materials. The packaging layer should
include the complete `COPYING.txt` with generated projects that link this
runtime; a provenance URL alone does not replace the notice.
