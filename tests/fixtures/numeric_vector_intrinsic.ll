; Expected exit_code: 0. Overloaded vector intrinsics operate per lane, including per-lane overflow flags.
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"

declare <4 x i8> @llvm.ctpop.v4i8(<4 x i8>)
declare <4 x i8> @llvm.abs.v4i8(<4 x i8>, i1 immarg)
declare { <4 x i8>, <4 x i1> } @llvm.uadd.with.overflow.v4i8(<4 x i8>, <4 x i8>)

define i32 @main() {
entry:
  %counts = call <4 x i8> @llvm.ctpop.v4i8(<4 x i8> <i8 255, i8 1, i8 3, i8 0>)
  %counts.bits = bitcast <4 x i8> %counts to i32
  %counts.ok = icmp eq i32 %counts.bits, 131336
  %absolute = call <4 x i8> @llvm.abs.v4i8(<4 x i8> <i8 -1, i8 -128, i8 7, i8 -3>, i1 false)
  %abs.expected = icmp eq <4 x i8> %absolute, <i8 1, i8 -128, i8 7, i8 3>
  %abs.mask = bitcast <4 x i1> %abs.expected to i4
  %abs.ok = icmp eq i4 %abs.mask, 15
  %sum = call { <4 x i8>, <4 x i1> } @llvm.uadd.with.overflow.v4i8(<4 x i8> <i8 255, i8 10, i8 250, i8 1>, <4 x i8> <i8 1, i8 20, i8 5, i8 255>)
  %sum.value = extractvalue { <4 x i8>, <4 x i1> } %sum, 0
  %sum.flags = extractvalue { <4 x i8>, <4 x i1> } %sum, 1
  %sum.expected = icmp eq <4 x i8> %sum.value, <i8 0, i8 30, i8 255, i8 0>
  %sum.mask = bitcast <4 x i1> %sum.expected to i4
  %sum.ok = icmp eq i4 %sum.mask, 15
  %flags.mask = bitcast <4 x i1> %sum.flags to i4
  %flags.ok = icmp eq i4 %flags.mask, 9
  %a = and i1 %counts.ok, %abs.ok
  %b = and i1 %sum.ok, %flags.ok
  %ok = and i1 %a, %b
  %failure = xor i1 %ok, true
  %result = zext i1 %failure to i32
  ret i32 %result
}
