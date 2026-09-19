target datalayout = "e-p:64:64-i64:64-i128:128-n8:16:32:64-S128"
declare i32 @llvm.ucmp.i32.i64(i64, i64)
declare i8 @llvm.scmp.i8.i64(i64, i64)
declare i128 @llvm.scmp.i128.i128(i128, i128)
declare i9 @llvm.ucmp.i9.i128(i128, i128)
declare i2 @llvm.scmp.i2.i1(i1, i1)
declare i2 @llvm.ucmp.i2.i1(i1, i1)
declare <4 x i3> @llvm.scmp.v4i3.v4i9(<4 x i9>, <4 x i9>)
declare <4 x i2> @llvm.ucmp.v4i2.v4i64(<4 x i64>, <4 x i64>)

define i32 @main() {
  %u0 = call i32 @llvm.ucmp.i32.i64(i64 -9223372036854775808, i64 9223372036854775807)
  %u1 = call i32 @llvm.ucmp.i32.i64(i64 9007199254740992, i64 9007199254740993)
  %u2 = call i32 @llvm.ucmp.i32.i64(i64 -1, i64 -1)
  %s0 = call i8 @llvm.scmp.i8.i64(i64 -9223372036854775808, i64 9223372036854775807)
  %s1 = call i8 @llvm.scmp.i8.i64(i64 -5, i64 -6)
  %s2 = call i8 @llvm.scmp.i8.i64(i64 -6, i64 -6)
  %w0 = call i128 @llvm.scmp.i128.i128(i128 -170141183460469231731687303715884105728, i128 170141183460469231731687303715884105727)
  %w1 = call i128 @llvm.scmp.i128.i128(i128 18446744073709551617, i128 18446744073709551616)
  %w2 = call i9 @llvm.ucmp.i9.i128(i128 -1, i128 170141183460469231731687303715884105727)
  %w3 = call i9 @llvm.ucmp.i9.i128(i128 18446744073709551616, i128 18446744073709551617)
  %b0 = call i2 @llvm.scmp.i2.i1(i1 true, i1 false)
  %b1 = call i2 @llvm.ucmp.i2.i1(i1 true, i1 false)
  %v0 = call <4 x i3> @llvm.scmp.v4i3.v4i9(<4 x i9> <i9 -256, i9 255, i9 -1, i9 -9>, <4 x i9> <i9 255, i9 -256, i9 -1, i9 -8>)
  %v1 = call <4 x i2> @llvm.ucmp.v4i2.v4i64(<4 x i64> <i64 -1, i64 0, i64 9007199254740993, i64 -1>, <4 x i64> <i64 0, i64 -1, i64 9007199254740992, i64 -1>)
  %c0 = icmp eq i32 %u0, 1
  %c1 = icmp eq i32 %u1, -1
  %c2 = icmp eq i32 %u2, 0
  %c3 = icmp eq i8 %s0, -1
  %c4 = icmp eq i8 %s1, 1
  %c5 = icmp eq i8 %s2, 0
  %c6 = icmp eq i128 %w0, -1
  %c7 = icmp eq i128 %w1, 1
  %c8 = icmp eq i9 %w2, 1
  %c9 = icmp eq i9 %w3, -1
  %c10 = icmp eq i2 %b0, -1
  %c11 = icmp eq i2 %b1, 1
  %vc0 = icmp eq <4 x i3> %v0, <i3 -1, i3 1, i3 0, i3 -1>
  %vc1 = icmp eq <4 x i2> %v1, <i2 1, i2 -1, i2 1, i2 0>
  %vb0 = bitcast <4 x i1> %vc0 to i4
  %vb1 = bitcast <4 x i1> %vc1 to i4
  %c12 = icmp eq i4 %vb0, -1
  %c13 = icmp eq i4 %vb1, -1
  %a1 = and i1 %c0, %c1
  %a2 = and i1 %a1, %c2
  %a3 = and i1 %a2, %c3
  %a4 = and i1 %a3, %c4
  %a5 = and i1 %a4, %c5
  %a6 = and i1 %a5, %c6
  %a7 = and i1 %a6, %c7
  %a8 = and i1 %a7, %c8
  %a9 = and i1 %a8, %c9
  %a10 = and i1 %a9, %c10
  %a11 = and i1 %a10, %c11
  %a12 = and i1 %a11, %c12
  %a13 = and i1 %a12, %c13
  %result = select i1 %a13, i32 74, i32 1
  ret i32 %result
}
