target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
; A one-bit result cannot distinguish -1, zero, and +1.
declare i1 @llvm.ucmp.i1.i64(i64, i64)
define i32 @main() {
  %a = call i1 @llvm.ucmp.i1.i64(i64 1, i64 2)
  %b = zext i1 %a to i32
  ret i32 %b
}
