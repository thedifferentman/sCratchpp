; EXPECT-ERROR: integer-to-float conversion requires an integer width from 1 through 64
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"
define i32 @main() {
entry:
  %value = sitofp i128 1 to double
  %result = fptosi double %value to i32
  ret i32 %result
}
