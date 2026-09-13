; Overlap in both directions, zero count with invalid pointers, and legal one-past GEP.
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"
@bytes = constant [8 x i8] [i8 1, i8 2, i8 3, i8 4, i8 5, i8 6, i8 7, i8 8]
@zero = global i64 0
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1 immarg)
declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1 immarg)
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1 immarg)

define i32 @main() {
entry:
  %buffer = alloca [8 x i8], align 1
  call void @llvm.memcpy.p0.p0.i64(ptr %buffer, ptr @bytes, i64 8, i1 false)
  %two = getelementptr i8, ptr %buffer, i64 2
  call void @llvm.memmove.p0.p0.i64(ptr %two, ptr %buffer, i64 6, i1 false)
  call void @llvm.memmove.p0.p0.i64(ptr %buffer, ptr %two, i64 6, i1 false)
  ; [1,2,3,4,5,6,5,6], including bytes outside the last destination.
  %raw = load i64, ptr %buffer, align 1
  %ok.copy = icmp eq i64 %raw, 433759557723030017
  %past = getelementptr inbounds [8 x i8], ptr %buffer, i64 0, i64 8
  %back = getelementptr i8, ptr %past, i64 -1
  %last = load i8, ptr %back, align 1
  %ok.past = icmp eq i8 %last, 6
  %invalid = inttoptr i64 -1 to ptr
  %high = inttoptr i64 4294967296 to ptr
  %count = load volatile i64, ptr @zero
  call void @llvm.memcpy.p0.p0.i64(ptr %invalid, ptr %high, i64 %count, i1 false)
  call void @llvm.memmove.p0.p0.i64(ptr %high, ptr %invalid, i64 %count, i1 false)
  call void @llvm.memset.p0.i64(ptr %invalid, i8 255, i64 %count, i1 false)
  %odd = alloca i17, align 4
  call void @llvm.memset.p0.i64(ptr %odd, i8 255, i64 3, i1 false)
  %narrow = load volatile i17, ptr %odd, align 1
  %wide = sext i17 %narrow to i64
  %ok.mask = icmp eq i64 %wide, -1
  %bool = load volatile i1, ptr %odd, align 1
  %ok.first = and i1 %ok.copy, %ok.past
  %ok.second = and i1 %ok.mask, %bool
  %ok = and i1 %ok.first, %ok.second
  %failure = xor i1 %ok, true
  %result = zext i1 %failure to i32
  ret i32 %result
}
