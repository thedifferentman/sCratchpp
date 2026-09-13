; EXPECT: main returns 42. Global basic-block pointer relocation and indirect dispatch.
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
@destinations = global [2 x ptr] [ptr blockaddress(@route, %left), ptr blockaddress(@route, %right)], align 8
define i32 @route(i64 %index) {
entry:
  %slot = getelementptr [2 x ptr], ptr @destinations, i64 0, i64 %index
  %destination = load ptr, ptr %slot, align 8
  indirectbr ptr %destination, [label %left, label %right]
left:
  br label %finish
right:
  br label %finish
finish:
  %value = phi i32 [ 20, %left ], [ 22, %right ]
  ret i32 %value
}
define i32 @main() {
entry:
  %a = call i32 @route(i64 0)
  %b = call i32 @route(i64 1)
  %result = add i32 %a, %b
  ret i32 %result
}
