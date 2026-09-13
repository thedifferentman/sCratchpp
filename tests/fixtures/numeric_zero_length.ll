; Expected exit_code: 0. Zero-byte memory intrinsics do not dereference either pointer.
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"

declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1 immarg)
declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1 immarg)
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1 immarg)

define i32 @main() {
entry:
  %outside = inttoptr i64 -1 to ptr
  call void @llvm.memcpy.p0.p0.i64(ptr %outside, ptr null, i64 0, i1 false)
  call void @llvm.memmove.p0.p0.i64(ptr null, ptr %outside, i64 0, i1 false)
  call void @llvm.memset.p0.i64(ptr %outside, i8 255, i64 0, i1 false)
  ret i32 0
}
