; EXPECT: main returns 61. Includes a parallel phi swap and byte aliasing.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@values = global [4 x i32] [i32 3, i32 5, i32 7, i32 11], align 4

define i32 @main() {
entry:
  %scratch = alloca i32, align 4
  store i32 16909060, ptr %scratch, align 4
  %byteptr = getelementptr i8, ptr %scratch, i64 2
  %byte = load i8, ptr %byteptr, align 1
  %byte32 = zext i8 %byte to i32
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %newsum, %loop ]
  %a = phi i32 [ 10, %entry ], [ %b, %loop ]
  %b = phi i32 [ 20, %entry ], [ %a, %loop ]
  %p = getelementptr [4 x i32], ptr @values, i64 0, i64 %i
  %v = load i32, ptr %p, align 4
  %newsum = add i32 %sum, %v
  %next = add i64 %i, 1
  %again = icmp ult i64 %next, 3
  br i1 %again, label %loop, label %done
done:
  %x = add i32 %newsum, %a
  %y = add i32 %x, %byte32
  %r = add i32 %y, 34
  ret i32 %r
}
