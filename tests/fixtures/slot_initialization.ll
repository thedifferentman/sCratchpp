target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
@large = global [80 x i8] c"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
@odd = global i9 511
define i32 @choose(i1 %flag) {
  %r = select i1 %flag, i32 1, i32 0
  ret i32 %r
}
define i32 @main() {
  %p = getelementptr [80 x i8], ptr @large, i64 0, i64 79
  %byte = load i8, ptr %p
  %b = zext i8 %byte to i32
  %n = load i9, ptr @odd
  %w = zext i9 %n to i32
  %flag = call i32 @choose(i1 true)
  %sum = add i32 %b, %w
  %sum2 = add i32 %sum, %flag
  %result = sub i32 %sum2, 577
  ret i32 %result
}
