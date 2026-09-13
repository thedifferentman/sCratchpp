; EXPECT: main returns 41. Atomic old-value and cmpxchg success/failure semantics.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@counter = thread_local global i32 10, align 4

define i32 @main() {
entry:
  %old = atomicrmw add ptr @counter, i32 5 seq_cst, align 4
  %first = cmpxchg ptr @counter, i32 15, i32 21 seq_cst seq_cst, align 4
  %firstold = extractvalue { i32, i1 } %first, 0
  %success = extractvalue { i32, i1 } %first, 1
  %second = cmpxchg ptr @counter, i32 99, i32 100 seq_cst seq_cst, align 4
  %failed = extractvalue { i32, i1 } %second, 1
  fence seq_cst
  %now = load atomic volatile i32, ptr @counter seq_cst, align 4
  %good = and i1 %success, true
  %bad = xor i1 %failed, true
  %checks = and i1 %good, %bad
  %selected = select i1 %checks, i32 %firstold, i32 0
  %sum = add i32 %old, %selected
  %total = add i32 %sum, %now
  %r = sub i32 %total, 5
  ret i32 %r
}
