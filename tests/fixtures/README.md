# LLVM IR end-to-end fixtures

`manifest.json` specifies the exact `main` result and little-endian result bytes.
These fixtures use an explicit little-endian 64-bit pointer layout; they contain
no host runtime or library dependencies. Clang 22.1.8 successfully parsed each
fixture and emitted LLVM bitcode during creation.

- `integer.ll`: byte carries, wrapping and signed versus unsigned operations.
- `recursion.ll`: factorial and Fibonacci, including values live across nested calls.
- `memory_phi.ll`: globals, GEP, aliasing through an `i8` load and simultaneous phi assignment.
- `function_pointer.ll`: relocated function tables, indirect calls and pointer identity.
- `integer_wide.ll`: exact i64 multiplication/division, arithmetic right shift and odd integer widths.
- `global_aggregate.ll`: packed unaligned storage, nested arrays, global pointer initializers and aliases.
- `call_aggregate.ll`: aggregate SSA values/returns, `sret`, and `byval` copy isolation.
- `atomic_tls.ll`: atomic old values, successful and failed `cmpxchg`, TLS and volatile access.
- `memory_intrinsics.ll`: byte copying, overlapping movement and zero-length null operations.
- `stack_restore.ll`: dynamic allocation and values live across stack restoration.
- `vector_bits.ll`: non-byte vector elements, masks, shuffle and memory packing.
- `global_ctors.ll`: priority-ordered initializers and preserving `main`'s result across destructors.
- `intrinsic_overflow.ll`: overflow flags, bit counting, byte swaps and signed saturation.
- `narrow_load.ll`: loading `i1/i9` from bytes with nonzero unused storage bits.
- `numeric_*.ll`: independent numeric-agent regressions for pointer index widths,
  zero-length memory operations, packed vectors, atomic operations and poison.
- `musttail_*.ll`: 2500 or more self/mutual tail calls and `byval` transfers,
  compiled with a 4096-byte memory budget.
- `float_*.ll`: byte-exact float/double operations, intrinsics, vectors,
  comparison predicates and sign-bit operations; native reference expectations
  were supplied by the floating-point runtime tests.
- `varargs_ir.ll` and `varargs_sysv.ll`: scalar x86-64 System V variadic calls,
  both raw `va_arg` and Clang-generated `va_list` storage operations. The sibling
  negative fixtures check unsupported ABI and argument categories.
- `blockaddress_global.ll` and `indirectbr_phi.ll`: basic-block address relocation
  and phi assignments on indirect predecessor/back edges.
- `main_args.ll` and `main_args_utf8.ll`: `argc/argv`, both CLI argument forms,
  null terminators and UTF-8 bytes in the program memory list.
- `masked_memory.ll` and `masked_scatter.ll`: inactive lanes with invalid/high-bit
  pointers, passthrough values and ordered overlapping scatter. The declarations
  use the actual LLVM 22 intrinsic signatures verified with Clang 22.1.8.
- `assembly_named.ll`: full Scratch assembly templates; `unused_asm_suffix`
  checks that optimization cannot hide an invalid instruction in an unused function.
- `review_vector_gep.ll`, `review_ctor_alias.ll` and `negative/oversized_alloca.ll`:
  independent review regressions for vector address calculation, constructor
  aliases and layout size truncation. `clang_asm_clobbers.ll` preserves actual
  Clang-generated IR with its automatic machine-state clobbers.
- `negative/`: required frontend rejection of missing layouts, unsupported big endian,
  machine assembly and invalid SSA dominance.

The real VM harness is installed and invoked from the project root:

```text
npm --prefix tests ci --ignore-scripts
npm --prefix tests test
node tests/vm_runner.cjs build/program.sb3 --vm scratch --timeout 30000
node tests/vm_runner.cjs build/program.sb3 --vm turbowarp --timeout 30000
node tests/e2e.cjs --vm both --output-dir build/test-output/e2e
node tests/e2e.cjs --case "negative/*" --output-dir build/test-output/negative
```

Compiler selection is `--compiler path-or-command`, then `SCRATCH_COMPILER`,
then `build/clang/scratch-llvm` (`scratch-llvm.exe` on Windows), then `PATH`.
An explicitly selected compiler that is missing fails instead of falling back.
The legacy `build/native/Release` MSVC binary is never an automatic fallback.
CTest always supplies the exact compiler target path and a build-specific output
directory. For simultaneous runs, pass distinct `--output-dir` directories.
`varargs_test.cjs` accepts the same `--compiler` and `--output-dir` options.

The harness returns one JSON object on stdout; exit code is 0 for completion,
1 for a VM error or timeout, and 2 for invalid CLI arguments. `variables` and
`lists` contain values by unique display name. `targets` preserves all values
when names overlap across targets. VM numeric literals can remain strings in
Scratch and become numbers in TurboWarp; raw values are deliberately preserved.
`--list-limit 256` limits each output list to its first 256 entries while
`listLengths` retains the full lengths.

Execution uses actual upstream `scratch-vm` and the pinned TurboWarp VM compiler.
No Scratch arithmetic or control-flow opcode is replaced. Original Scratch runs
interpreted; TurboWarp must compile without `COMPILE_ERROR`. The report includes
observed `compiledThreads`, not just whether a compiler option was requested.
TurboWarp's default `warpTimer: false` is retained; `--warp-timer` enables its
warp-time checks. A separate controller process enforces hard timeouts even if
compiled warp code never yields.

SVG files are loaded by real `scratch-storage`, but this harness does not attach
a renderer or audio engine. It verifies computation and project loading, not
pixels or audio output. The runner tests load and execute generated SB3 fixtures
under `tests/.tmp/`; they do not execute the `.ll` files themselves. End-to-end
compiler tests must compile the `.ll` fixtures before passing their SB3 to the
runner.

`e2e.cjs` compiles every selected IR fixture, executes each successful compilation
in the selected real VM(s), and checks `__scl_status`, `exit_code` and exact
`return_bytes`. Compiler failures, VM failures, result mismatches and missing
negative diagnostics are distinct report categories. Its default report is
`<output-dir>/report.json`; use `--report path` to retain another report.
The default output directory is `tests/.tmp/e2e/<platform>-<arch>`, keeping host
platforms separate; `--output-dir path` separates parallel builds on one host.
`--case` and `--exclude` accept globs and can be repeated. Negative fixtures must fail with a
matching diagnostic and must not leave a generated SB3. No compiler feature is
silently skipped when it is not implemented.

Individual fixtures may set `compilerArgs` in the manifest, for example a small
memory budget for a tail-call test. Reports record compiler modification times
to identify runs made across a rebuild. The VM keeps the default Node heap limit;
heap exhaustion is a runtime resource failure, not a reason to silently increase
the heap or fall back to interpretation.

`loadMs` includes dependency initialization and project loading. `executionMs`
includes green-flag compilation and scheduler waits. These are headless
end-to-end timings, not direct substitutes for interactive TurboWarp benchmarks.
`threadCompilationMs` measures TurboWarp's compiled-thread creation (including
cached function setup); `executionMinusThreadCompilationMs` subtracts that time
but still includes scheduler waits and any JavaScript engine JIT work during
execution. `memory` records process peak RSS, final heap use and configured heap
limit; the runner does not raise that limit.
