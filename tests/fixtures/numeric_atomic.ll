; Expected exit_code: 0. Atomic results expose the old value and cmpxchg's success flag, including padded aggregate returns.
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"

@word = global i64 -9223372036854775808, align 8

define i32 @main() {
entry:
  %old.max = atomicrmw max ptr @word, i64 -1 seq_cst
  %old.min = atomicrmw umin ptr @word, i64 17 monotonic
  %old.nand = atomicrmw nand ptr @word, i64 15 monotonic
  %failed = cmpxchg ptr @word, i64 17, i64 42 seq_cst monotonic
  %failed.old = extractvalue { i64, i1 } %failed, 0
  %failed.flag = extractvalue { i64, i1 } %failed, 1
  %succeeded = cmpxchg ptr @word, i64 -2, i64 42 acq_rel acquire
  %succeeded.old = extractvalue { i64, i1 } %succeeded, 0
  %succeeded.flag = extractvalue { i64, i1 } %succeeded, 1
  %now = load atomic i64, ptr @word acquire, align 8
  %a = icmp eq i64 %old.max, -9223372036854775808
  %b = icmp eq i64 %old.min, -1
  %c = icmp eq i64 %old.nand, 17
  %d = icmp eq i64 %failed.old, -2
  %e = icmp eq i64 %succeeded.old, -2
  %f = icmp eq i64 %now, 42
  %fail.flag = xor i1 %failed.flag, true
  %ab = and i1 %a, %b
  %cd = and i1 %c, %d
  %ef = and i1 %e, %f
  %flags = and i1 %fail.flag, %succeeded.flag
  %abcd = and i1 %ab, %cd
  %efflags = and i1 %ef, %flags
  %ok = and i1 %abcd, %efflags
  %failure = xor i1 %ok, true
  %result = zext i1 %failure to i32
  ret i32 %result
}
