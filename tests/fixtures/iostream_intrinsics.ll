target datalayout = "e-p:64:64-i64:64-i128:128-n8:16:32:64-S128"
@lanes = global <4 x i9> <i9 0, i9 0, i9 256, i9 0>
@flags = global <4 x i1> <i1 false, i1 true, i1 false, i1 false>
@fixed = constant i32 5

declare i64 @llvm.experimental.cttz.elts.i64.v4i9(<4 x i9>, i1 immarg)
declare i32 @llvm.experimental.cttz.elts.i32.v4i1(<4 x i1>, i1 immarg)
declare ptr @llvm.invariant.start.p0(i64 immarg, ptr)
declare void @llvm.invariant.end.p0(ptr, i64 immarg, ptr)

define i32 @main() {
  %marker = call ptr @llvm.invariant.start.p0(i64 4, ptr @fixed)
  %x = load volatile i32, ptr @fixed
  call void @llvm.invariant.end.p0(ptr %marker, i64 4, ptr @fixed)
  %v = load volatile <4 x i9>, ptr @lanes
  %b = load volatile <4 x i1>, ptr @flags
  %a = call i64 @llvm.experimental.cttz.elts.i64.v4i9(<4 x i9> %v, i1 true)
  %z = call i64 @llvm.experimental.cttz.elts.i64.v4i9(<4 x i9> zeroinitializer, i1 false)
  %c = call i32 @llvm.experimental.cttz.elts.i32.v4i1(<4 x i1> %b, i1 false)
  %sum = add i64 %a, %z
  %small = trunc i64 %sum to i32
  %more = add i32 %small, %c
  %result = add i32 %more, %x
  ret i32 %result
}
