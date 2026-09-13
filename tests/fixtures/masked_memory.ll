; EXPECT: main returns 367. Inactive lanes must not decode or access invalid pointers.
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
@source = global [4 x i32] [i32 10, i32 20, i32 30, i32 40], align 4
@destination = global [4 x i32] [i32 1, i32 2, i32 3, i32 4], align 4
declare <4 x i32> @llvm.masked.load.v4i32.p0(ptr, <4 x i1>, <4 x i32>)
declare void @llvm.masked.store.v4i32.p0(<4 x i32>, ptr, <4 x i1>)
declare <4 x i32> @llvm.masked.gather.v4i32.v4p0(<4 x ptr>, <4 x i1>, <4 x i32>)
define i32 @sum4(<4 x i32> %values) {
entry:
  %a = extractelement <4 x i32> %values, i32 0
  %b = extractelement <4 x i32> %values, i32 1
  %c = extractelement <4 x i32> %values, i32 2
  %d = extractelement <4 x i32> %values, i32 3
  %x = add i32 %a, %b
  %y = add i32 %c, %d
  %r = add i32 %x, %y
  ret i32 %r
}
define i32 @main() {
entry:
  %loaded = call <4 x i32> @llvm.masked.load.v4i32.p0(ptr align 4 @source, <4 x i1> <i1 true, i1 false, i1 true, i1 false>, <4 x i32> <i32 101, i32 102, i32 103, i32 104>)
  call void @llvm.masked.store.v4i32.p0(<4 x i32> <i32 11, i32 12, i32 13, i32 14>, ptr align 4 @destination, <4 x i1> <i1 true, i1 false, i1 true, i1 false>)
  %stored = load <4 x i32>, ptr @destination, align 4
  %p1 = getelementptr i32, ptr @source, i64 1
  %p3 = getelementptr i32, ptr @source, i64 3
  %bad = inttoptr i64 -1 to ptr
  %high = inttoptr i64 4294967296 to ptr
  %ptr0 = insertelement <4 x ptr> poison, ptr %p1, i32 0
  %ptr1 = insertelement <4 x ptr> %ptr0, ptr %bad, i32 1
  %ptr2 = insertelement <4 x ptr> %ptr1, ptr %high, i32 2
  %ptrs = insertelement <4 x ptr> %ptr2, ptr %p3, i32 3
  %gathered = call <4 x i32> @llvm.masked.gather.v4i32.v4p0(<4 x ptr> %ptrs, <4 x i1> <i1 true, i1 false, i1 false, i1 true>, <4 x i32> <i32 1, i32 2, i32 3, i32 4>)
  %inactive = call <4 x i32> @llvm.masked.load.v4i32.p0(ptr %bad, <4 x i1> zeroinitializer, <4 x i32> <i32 5, i32 6, i32 7, i32 8>)
  call void @llvm.masked.store.v4i32.p0(<4 x i32> zeroinitializer, ptr %high, <4 x i1> zeroinitializer)
  %a = call i32 @sum4(<4 x i32> %loaded)
  %b = call i32 @sum4(<4 x i32> %stored)
  %c = call i32 @sum4(<4 x i32> %gathered)
  %d = call i32 @sum4(<4 x i32> %inactive)
  %x = add i32 %a, %b
  %y = add i32 %c, %d
  %result = add i32 %x, %y
  ret i32 %result
}
