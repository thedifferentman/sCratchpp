; A 4 GiB allocation must not silently truncate its layout size to zero.
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
define i32 @main() {
entry:
  %buffer = alloca [4294967296 x i8], align 1
  store i8 1, ptr %buffer, align 1
  ret i32 0
}
