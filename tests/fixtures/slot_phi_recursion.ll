; Direct-slot lowering must preserve parallel cycles, recursive SSA and constants.
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i64 @recur(i32 %n, i64 %seed) {
entry:
  %base = icmp eq i32 %n, 0
  br i1 %base, label %done, label %step
done:
  ret i64 %seed
step:
  %product = mul i64 %seed, 257
  %wide = zext i32 %n to i64
  %live = add i64 %product, %wide
  %next = sub i32 %n, 1
  %seed.next = add i64 %seed, 3
  %nested = call i64 @recur(i32 %next, i64 %seed.next)
  ; A second call and constant operands must not overwrite live/nested slots.
  %sibling = call i64 @recur(i32 0, i64 4294967297)
  %sum = add i64 %live, %nested
  %result = xor i64 %sum, %sibling
  ret i64 %result
}

define i32 @main() {
entry:
  %recursive = call i64 @recur(i32 4, i64 65539)
  br label %cycle
cycle:
  %i = phi i32 [ 0, %entry ], [ %next, %cycle ]
  %a = phi i64 [ 1, %entry ], [ %b, %cycle ]
  %b = phi i64 [ 2, %entry ], [ %c, %cycle ]
  %c = phi i64 [ 3, %entry ], [ %a, %cycle ]
  %fanout = phi i64 [ 9, %entry ], [ %a, %cycle ]
  %next = add i32 %i, 1
  %again = icmp ult i32 %next, 5
  br i1 %again, label %cycle, label %exit
exit:
  %ok.a = icmp eq i64 %a, 2
  %ok.b = icmp eq i64 %b, 3
  %ok.c = icmp eq i64 %c, 1
  %ok.fanout = icmp eq i64 %fanout, 1
  %ok.recursive = icmp eq i64 %recursive, 67444283
  %ok.ab = and i1 %ok.a, %ok.b
  %ok.cd = and i1 %ok.c, %ok.fanout
  %ok.cycle = and i1 %ok.ab, %ok.cd
  %ok = and i1 %ok.cycle, %ok.recursive
  %failure = xor i1 %ok, true
  %result = zext i1 %failure to i32
  ret i32 %result
}
