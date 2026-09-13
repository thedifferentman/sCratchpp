; EXPECT: main returns 50. Global constructors execute in priority order.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"
@state = global i32 0, align 4
@llvm.global_ctors = appending global [2 x { i32, ptr, ptr }] [
  { i32, ptr, ptr } { i32 200, ptr @second, ptr null },
  { i32, ptr, ptr } { i32 100, ptr @first, ptr null }
]
@llvm.global_dtors = appending global [1 x { i32, ptr, ptr }] [
  { i32, ptr, ptr } { i32 100, ptr @cleanup, ptr null }
]
define void @first() {
entry:
  store i32 5, ptr @state, align 4
  ret void
}
define void @second() {
entry:
  %old = load i32, ptr @state, align 4
  %new = mul i32 %old, 10
  store i32 %new, ptr @state, align 4
  ret void
}
define void @cleanup() {
entry:
  store i32 99, ptr @state, align 4
  ret void
}
define i32 @main() {
entry:
  %r = load i32, ptr @state, align 4
  ret i32 %r
}
