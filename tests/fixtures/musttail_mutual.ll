; EXPECT: main returns 6252 even with --memory 4096.
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
define i32 @left(i32 %n, i32 %accumulator) {
entry:
  %done = icmp eq i32 %n, 0
  br i1 %done, label %finish, label %step
finish:
  ret i32 %accumulator
step:
  %next = sub i32 %n, 1
  %sum = add i32 %accumulator, 2
  %result = musttail call i32 @right(i32 %next, i32 %sum)
  ret i32 %result
}
define i32 @right(i32 %n, i32 %accumulator) {
entry:
  %done = icmp eq i32 %n, 0
  br i1 %done, label %finish, label %step
finish:
  ret i32 %accumulator
step:
  %next = sub i32 %n, 1
  %sum = add i32 %accumulator, 3
  %result = musttail call i32 @left(i32 %next, i32 %sum)
  ret i32 %result
}
define i32 @main() {
entry:
  %result = call i32 @left(i32 2501, i32 0)
  ret i32 %result
}
