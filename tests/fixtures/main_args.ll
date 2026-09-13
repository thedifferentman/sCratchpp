target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"
define i32 @main(i32 %argc, ptr %argv) {
entry:
  %arg1slot = getelementptr ptr, ptr %argv, i64 1
  %arg1 = load ptr, ptr %arg1slot
  %first = load i8, ptr %arg1
  %wide = zext i8 %first to i32
  %result = add i32 %argc, %wide
  %end = getelementptr ptr, ptr %argv, i32 %argc
  %null = load ptr, ptr %end
  %ok = icmp eq ptr %null, null
  %final = select i1 %ok, i32 %result, i32 999
  ret i32 %final
}
