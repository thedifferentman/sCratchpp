; EXPECT: main returns 47. Function pointers are initialized, stored and loaded.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@dispatch = global [2 x ptr] [ptr @increment, ptr @triple], align 8

define i32 @increment(i32 %x) align 4 {
entry:
  %r = add i32 %x, 1
  ret i32 %r
}

define i32 @triple(i32 %x) align 4 {
entry:
  %r = mul i32 %x, 3
  ret i32 %r
}

define i32 @apply(ptr %fn, i32 %x) {
entry:
  %r = call i32 %fn(i32 %x)
  ret i32 %r
}

define i32 @main() {
entry:
  %p = getelementptr [2 x ptr], ptr @dispatch, i64 0, i64 1
  %fn = load ptr, ptr %p, align 8
  %x = call i32 @apply(ptr %fn, i32 15)
  %f0 = load ptr, ptr @dispatch, align 8
  %y = call i32 %f0(i32 %x)
  %unequal = icmp ne ptr %f0, @dispatch
  %one = zext i1 %unequal to i32
  %r = add i32 %y, %one
  ret i32 %r
}
