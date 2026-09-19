# First-batch C++ standard library integration tests

Build the target SDK with `tools/build_stdlib.py`, then run:

```sh
python tests/stdlib_test.py --compiler build/clang/scratch-llvm --sdk build/clang/stdlib
```

On Windows use the `.exe` executable. Pass `--clang`, `--node`, or activate the
chosen toolchain when these executables are not on `PATH`. The SDK compiler
configuration controls guest headers and ABI; host C++ headers are not used.

The default runs every fixture on both the official Scratch VM and compiled
TurboWarp VM. `--case 'lifecycle_*'`, `--vm scratch`, and `--output-dir PATH`
select focused runs. `--timeout` is the per-program execution limit in
milliseconds, default 180000; module loading has its own deadline.

Container groups are separate projects to bound generated block counts and VM
loading/compilation cost. The combined vector/deque/list/string fixture is kept
as `--include-stress --case sequences_stress`: its first Windows official-VM
run exceeded the 60-second execution limit with a roughly 90 MB uncompressed
project. This is an outstanding large-program performance limitation, not a
passing combined-workload result. The normal suite retains the individual checks.

The isolated vector fixture also exceeded 60 seconds in TurboWarp's combined
compile/execution phase, while completing in official Scratch. For correctness
validation of this first backend, the default is now `--timeout 180000`; this is an explicitly
larger bound for generated JavaScript compilation and execution, not a performance
guarantee. Reports retain the VM's compilation timing and the selected bound.

The combined associative and optional combined sequences fixtures use a minimum
600000 ms bound in the official Scratch interpreter, while TurboWarp retains the
180000 ms default. The associative interpreter run reached the earlier 180000 ms
cutoff; its compiled TurboWarp execution took about half a second after compilation.
These explicit, hardware-sensitive validation limits do not change VM heap limits
or program behavior. Every run records its effective bound.

Coverage includes overlapping memory moves, byte comparison, string primitives,
allocation alignment/overflow, calloc zeroing, realloc preservation on failure,
free-block coalescing, allocation/deallocation forms, new-handler recovery,
global/local-static
initialization, destructor/atexit ordering, immediate and quick exit, terminal
error paths, DSO-filtered/reentrant finalization, allocation overrides, and
representative containers, algorithms and ownership utilities.
This is integration coverage for the no-exceptions target profile, not a claim
of complete C++ standard conformance. Exception-raising operations are tested as
terminal failures in this profile.

The current no-exceptions SDK requires a matching nothrow allocation replacement
when code combines a custom throwing allocation function with nothrow allocation.
The paired override must work; using the unmatched default nothrow implementation
is an explicit terminal error and is covered as a profile restriction.

Generated bitcode, `.sb3`, extracted project JSON, VM snapshots, compiler/runtime
hashes and diagnostics are retained under `tests/.tmp/stdlib` by default.


The integration harness uses --whole-program and the SDK's memory_bytes recommendation,
matching scrate final linking. With the enlarged locale/stream SDK, conservative linking
without pruning may retain unsupported regional facets and unused conversion tables.
