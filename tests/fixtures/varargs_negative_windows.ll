target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc"
declare void @llvm.va_start.p0(ptr)
define i32 @f(i32 %n, ...) {
  %ap = alloca ptr
  call void @llvm.va_start.p0(ptr %ap)
  ret i32 0
}
define i32 @main() {
  %r = call i32 (i32, ...) @f(i32 0, i32 1)
  ret i32 %r
}
