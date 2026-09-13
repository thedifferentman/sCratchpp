; EXPECT: main returns 36. Phi assignments on indirect predecessor edges and loop backedges.
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
define i32 @compute(i1 %choose) {
entry:
  br i1 %choose, label %seed_left, label %seed_right
seed_left:
  indirectbr ptr blockaddress(@compute, %loop), [label %loop]
seed_right:
  indirectbr ptr blockaddress(@compute, %loop), [label %loop]
loop:
  %value = phi i32 [ 10, %seed_left ], [ 20, %seed_right ], [ %next, %body ]
  %count = phi i32 [ 0, %seed_left ], [ 0, %seed_right ], [ %increment, %body ]
  %done = icmp eq i32 %count, 3
  br i1 %done, label %finish, label %body
body:
  %next = add i32 %value, 1
  %increment = add i32 %count, 1
  indirectbr ptr blockaddress(@compute, %loop), [label %loop]
finish:
  ret i32 %value
}
define i32 @main() {
entry:
  %a = call i32 @compute(i1 true)
  %b = call i32 @compute(i1 false)
  %result = add i32 %a, %b
  ret i32 %result
}
