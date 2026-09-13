; EXPECT: main returns 3. Bits outside an integer's width are not part of its value.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"
@raw = global [4 x i8] [i8 2, i8 -1, i8 2, i8 -1], align 2
define i32 @main() {
entry:
  %zero = load i1, ptr @raw, align 1
  %falsecheck = select i1 %zero, i32 100, i32 1
  %other = getelementptr i8, ptr @raw, i64 1
  %one = load i1, ptr %other, align 1
  %truecheck = select i1 %one, i32 1, i32 100
  %oddptr = getelementptr i8, ptr @raw, i64 2
  %odd = load i9, ptr %oddptr, align 2
  %oddok = icmp eq i9 %odd, 258
  %oddcheck = select i1 %oddok, i32 1, i32 100
  %sum = add i32 %falsecheck, %truecheck
  %r = add i32 %sum, %oddcheck
  ret i32 %r
}
