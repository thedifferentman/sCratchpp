; EXPECT: main returns 44. Overlapping memmove and unaligned memcpy.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1 immarg)
declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1 immarg)
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1 immarg)
@bytes = constant [6 x i8] [i8 1, i8 2, i8 3, i8 4, i8 5, i8 6]

define i32 @main() {
entry:
  %buffer = alloca [8 x i8], align 1
  call void @llvm.memset.p0.i64(ptr %buffer, i8 9, i64 8, i1 false)
  call void @llvm.memcpy.p0.p0.i64(ptr %buffer, ptr @bytes, i64 6, i1 false)
  %dst = getelementptr i8, ptr %buffer, i64 2
  call void @llvm.memmove.p0.p0.i64(ptr %dst, ptr %buffer, i64 6, i1 false)
  ; Result [1,2,1,2,3,4,5,6], sum = 24.
  ; Zero length memory operations must not dereference a null pointer.
  call void @llvm.memcpy.p0.p0.i64(ptr null, ptr null, i64 0, i1 false)
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %next, %loop ]
  %sum = phi i32 [ 20, %entry ], [ %newsum, %loop ]
  %ptr = getelementptr i8, ptr %buffer, i64 %i
  %byte = load i8, ptr %ptr, align 1
  %value = zext i8 %byte to i32
  %newsum = add i32 %sum, %value
  %next = add i64 %i, 1
  %continue = icmp ult i64 %next, 8
  br i1 %continue, label %loop, label %done
done:
  ret i32 %newsum
}
