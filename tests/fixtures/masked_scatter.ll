; EXPECT: main returns 606. Overlapping active scatter addresses are written in lane order.
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
@data = global [4 x i32] [i32 1, i32 2, i32 3, i32 4], align 4
declare void @llvm.masked.scatter.v4i32.v4p0(<4 x i32>, <4 x ptr>, <4 x i1>)
define i32 @main() {
entry:
  %p2 = getelementptr i32, ptr @data, i64 2
  %bad = inttoptr i64 -1 to ptr
  %p0 = insertelement <4 x ptr> poison, ptr @data, i32 0
  %p1 = insertelement <4 x ptr> %p0, ptr @data, i32 1
  %pbad = insertelement <4 x ptr> %p1, ptr %bad, i32 2
  %pointers = insertelement <4 x ptr> %pbad, ptr %p2, i32 3
  call void @llvm.masked.scatter.v4i32.v4p0(<4 x i32> <i32 100, i32 200, i32 300, i32 400>, <4 x ptr> %pointers, <4 x i1> <i1 true, i1 true, i1 false, i1 true>)
  ; The second lane overwrites the first lane at the same address. The disabled
  ; third lane does not dereference -1. Final memory is [200,2,400,4].
  %values = load <4 x i32>, ptr @data, align 4
  %a = extractelement <4 x i32> %values, i32 0
  %b = extractelement <4 x i32> %values, i32 1
  %c = extractelement <4 x i32> %values, i32 2
  %d = extractelement <4 x i32> %values, i32 3
  %x = add i32 %a, %b
  %y = add i32 %c, %d
  %result = add i32 %x, %y
  ret i32 %result
}
