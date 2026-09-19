# Vendored libc++ 22.1.8

The headers and selected implementation files are LLVM Project libc++ from
`llvmorg-22.1.8`, under Apache-2.0 WITH LLVM-exception; see `LICENSE.TXT`.
`UPSTREAM.json` records the pinned source URL and the verified package from
which the installed headers were extracted. The package origin does not impose
a Windows dependency: the target configuration replaces the package's host
configuration and the build uses no host C or C++ include directories.

`include/__config_site` is the project's Scratch target configuration. It selects
ABI namespace `__1`, minimum header ABI 22 (no obsolete ABI compatibility
symbols), no threads, no filesystem, no random device, classic C locale, no wide-character
library, no timezone database, and serial parallel-algorithm fallback. Exceptions
and RTTI are disabled by compiler flags in the generated SDK manifest. UPSTREAM.json lists the small stream-initialization and classic-locale patches;
remaining headers and selected upstream implementation files are unmodified.

`SOURCE_SHA256SUMS` identifies the exact vendored inputs, including the local
configuration. `tools/build_stdlib.py` verifies these inputs and builds the
selected implementation files plus `runtime/stdlib/src` into one LLVM module.

The installed SDK contains the complete headers for consistent transitive
inclusion; their presence is not a claim that every API is implemented. Narrow streams and numeric string conversion now have a guest implementation.
Filesystem, named regional locales, threading, exception unwinding, RTTI, and
the complete mathematical library remain unsupported. Unsupported reachable external calls are diagnosed by
the Scratch linker/backend.
