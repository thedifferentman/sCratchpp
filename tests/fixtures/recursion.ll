; EXPECT: main returns 775 (factorial(6) + fibonacci(10)).
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i32 @factorial(i32 %n) {
entry:
  %small = icmp ule i32 %n, 1
  br i1 %small, label %base, label %step
base:
  ret i32 1
step:
  %previous = sub i32 %n, 1
  %recur = call i32 @factorial(i32 %previous)
  %result = mul i32 %n, %recur
  ret i32 %result
}

define i32 @fibonacci(i32 %n) {
entry:
  %small = icmp ult i32 %n, 2
  br i1 %small, label %base, label %step
base:
  ret i32 %n
step:
  %one = sub i32 %n, 1
  %two = sub i32 %n, 2
  %a = call i32 @fibonacci(i32 %one)
  %b = call i32 @fibonacci(i32 %two)
  %sum = add i32 %a, %b
  ret i32 %sum
}

define i32 @main() {
entry:
  %a = call i32 @factorial(i32 6)
  %b = call i32 @fibonacci(i32 10)
  %sum = add i32 %a, %b
  ret i32 %sum
}
