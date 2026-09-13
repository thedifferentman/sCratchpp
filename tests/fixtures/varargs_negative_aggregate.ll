target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"
define i32 @f(i32 %n, ...) { ret i32 %n }
define i32 @main() {
  %r = call i32 (i32, ...) @f(i32 0, {i64,i64} {i64 1,i64 2})
  ret i32 %r
}
