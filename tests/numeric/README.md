# Exact integer code generation tests

`numeric_test.cpp` executes the generated AST through an independent interpreter
of the Scratch arithmetic, variable, list and control opcodes. Arithmetic inside
that interpreter uses `double`, including Scratch's nonnegative modulo. It does
not directly call the compiler's byte algorithms.

The default suite checks 33,488 boundary and deterministic randomized cases for
1, 2, 7, 8, 9, 16, 17, 31, 32, 33, 63 and 64-bit integers. It covers arithmetic,
bitwise operations, shifts, signed/unsigned division and remainder, comparisons,
casts, bit counts/reversal, min/max, absolute value, funnel shifts, saturation,
and overflow results. Shift amounts outside the width and division by zero are
checked only against this backend's chosen finite concretization, not claimed
as generally defined LLVM behavior.

Passing `wide_cases.json` adds 1,248 arbitrary-precision reference cases for
65, 73, 127, 128, 129, 191, 256 and 257-bit integers. Python's built-in integers
generate these expected results independently. Fixtures are checked in, so
normal test execution does not require Python. Regenerate with:

```powershell
python tests/numeric/generate_wide_cases.py
```

The executable takes optional arguments:

```text
numeric-test [wide_cases.json] [numeric-smoke.sb3]
```

The second argument produces a Scratch project exercising 17/64-bit operations,
including multiplication whose intermediate exceeds double's exact integer
range. Run this artifact in the actual VMs using:

```powershell
node tests/vm_runner.cjs tests/.tmp/numeric-smoke.sb3 --vm scratch --list-limit 256
node tests/vm_runner.cjs tests/.tmp/numeric-smoke.sb3 --vm turbowarp --list-limit 256
```

Successful execution sets `__test_done` to 1 and leaves `__test_failures` at 0.
The `__test_actual` and `__test_expected` lists contain 145 matching bytes.
Compare list values numerically: the original Scratch VM may retain numeric
literal inputs as strings, whereas TurboWarp often materializes numbers.

The smoke project checks VM integration rather than serving as a steady-state
performance benchmark; VM startup and TurboWarp compilation affect its timings.

## Shared runtime helpers

`shared_numeric_test.cpp` builds a separate smoke project using `Numeric(true)`.
Compile it with the same `numeric.cpp` / `blocks.cpp` sources and include paths,
then run `shared-numeric-test output.sb3` and execute the output in both VMs.

The generator checks that identical signatures reuse one procedure and that
different call sites receive distinct output snapshots. The emitted project
uses 135 helper signatures and performs 684 byte checks, including a second
call before inspecting the first result, chained operations, non-byte widths,
comparison, casts and scalar intrinsics. Successful execution sets
`__test_done=1`, `__test_failures=0`, and `__test_checks=684`.

The full AST interpreter suite remains on the default inline path. Shared
helpers use exactly that emitter for each cached body; helper signatures include
the operation, integer widths, and every operand's byte count. Each invocation
passes bytes as Scratch custom-block arguments and snapshots the helper's output
globals immediately after returning. Helpers do not call LLVM functions or one
another, so their temporary variables cannot be interrupted by a recursive
numeric helper invocation in the current single-execution-context model.
