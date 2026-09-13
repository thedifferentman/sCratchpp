; EXPECT: main returns 71. Exact integral rounding, default FP environment, vector lanes.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare float @llvm.ceil.f32(float)
declare float @llvm.floor.f32(float)
declare float @llvm.trunc.f32(float)
declare float @llvm.round.f32(float)
declare float @llvm.roundeven.f32(float)
declare float @llvm.rint.f32(float)
declare float @llvm.nearbyint.f32(float)
declare double @llvm.ceil.f64(double)
declare double @llvm.floor.f64(double)
declare double @llvm.trunc.f64(double)
declare double @llvm.round.f64(double)
declare double @llvm.roundeven.f64(double)
declare double @llvm.rint.f64(double)
declare double @llvm.nearbyint.f64(double)
declare <2 x float> @llvm.ceil.v2f32(<2 x float>)

define i32 @main() {
entry:
  %a0 = bitcast i32 1069547520 to float
  %r0 = call float @llvm.ceil.f32(float %a0)
  %b0 = bitcast float %r0 to i32
  %c0 = icmp eq i32 %b0, 1073741824
  %a1 = bitcast i32 3217031168 to float
  %r1 = call float @llvm.floor.f32(float %a1)
  %b1 = bitcast float %r1 to i32
  %c1 = icmp eq i32 %b1, 3221225472
  %a2 = bitcast i32 3204448256 to float
  %r2 = call float @llvm.trunc.f32(float %a2)
  %b2 = bitcast float %r2 to i32
  %c2 = icmp eq i32 %b2, 2147483648
  %a3 = bitcast i32 1075838976 to float
  %r3 = call float @llvm.round.f32(float %a3)
  %b3 = bitcast float %r3 to i32
  %c3 = icmp eq i32 %b3, 1077936128
  %a4 = bitcast i32 1075838976 to float
  %r4 = call float @llvm.roundeven.f32(float %a4)
  %b4 = bitcast float %r4 to i32
  %c4 = icmp eq i32 %b4, 1073741824
  %a5 = bitcast i32 1080033280 to float
  %r5 = call float @llvm.rint.f32(float %a5)
  %b5 = bitcast float %r5 to i32
  %c5 = icmp eq i32 %b5, 1082130432
  %a6 = bitcast i32 3223322624 to float
  %r6 = call float @llvm.nearbyint.f32(float %a6)
  %b6 = bitcast float %r6 to i32
  %c6 = icmp eq i32 %b6, 3221225472
  %a7 = bitcast i32 2147483649 to float
  %r7 = call float @llvm.ceil.f32(float %a7)
  %b7 = bitcast float %r7 to i32
  %c7 = icmp eq i32 %b7, 2147483648
  %a8 = bitcast i32 1 to float
  %r8 = call float @llvm.floor.f32(float %a8)
  %b8 = bitcast float %r8 to i32
  %c8 = icmp eq i32 %b8, 0
  %a9 = bitcast i32 2147483648 to float
  %r9 = call float @llvm.roundeven.f32(float %a9)
  %b9 = bitcast float %r9 to i32
  %c9 = icmp eq i32 %b9, 2147483648
  %a10 = bitcast i32 2139095040 to float
  %r10 = call float @llvm.round.f32(float %a10)
  %b10 = bitcast float %r10 to i32
  %c10 = icmp eq i32 %b10, 2139095040
  %a11 = bitcast i32 4286578688 to float
  %r11 = call float @llvm.trunc.f32(float %a11)
  %b11 = bitcast float %r11 to i32
  %c11 = icmp eq i32 %b11, 4286578688
  %a12 = bitcast i32 2139095041 to float
  %r12 = call float @llvm.ceil.f32(float %a12)
  %b12 = bitcast float %r12 to i32
  %c12 = icmp eq i32 %b12, 2143289345
  %a13 = bitcast i32 1266679808 to float
  %r13 = call float @llvm.floor.f32(float %a13)
  %b13 = bitcast float %r13 to i32
  %c13 = icmp eq i32 %b13, 1266679808
  %a14 = bitcast i64 4609434218613702656 to double
  %r14 = call double @llvm.ceil.f64(double %a14)
  %b14 = bitcast double %r14 to i64
  %c14 = icmp eq i64 %b14, 4611686018427387904
  %a15 = bitcast i64 13832806255468478464 to double
  %r15 = call double @llvm.floor.f64(double %a15)
  %b15 = bitcast double %r15 to i64
  %c15 = icmp eq i64 %b15, 13835058055282163712
  %a16 = bitcast i64 13826050856027422720 to double
  %r16 = call double @llvm.trunc.f64(double %a16)
  %b16 = bitcast double %r16 to i64
  %c16 = icmp eq i64 %b16, 9223372036854775808
  %a17 = bitcast i64 4612811918334230528 to double
  %r17 = call double @llvm.round.f64(double %a17)
  %b17 = bitcast double %r17 to i64
  %c17 = icmp eq i64 %b17, 4613937818241073152
  %a18 = bitcast i64 4612811918334230528 to double
  %r18 = call double @llvm.roundeven.f64(double %a18)
  %b18 = bitcast double %r18 to i64
  %c18 = icmp eq i64 %b18, 4611686018427387904
  %a19 = bitcast i64 4615063718147915776 to double
  %r19 = call double @llvm.rint.f64(double %a19)
  %b19 = bitcast double %r19 to i64
  %c19 = icmp eq i64 %b19, 4616189618054758400
  %a20 = bitcast i64 13836183955189006336 to double
  %r20 = call double @llvm.nearbyint.f64(double %a20)
  %b20 = bitcast double %r20 to i64
  %c20 = icmp eq i64 %b20, 13835058055282163712
  %a21 = bitcast i64 9223372036854775809 to double
  %r21 = call double @llvm.ceil.f64(double %a21)
  %b21 = bitcast double %r21 to i64
  %c21 = icmp eq i64 %b21, 9223372036854775808
  %a22 = bitcast i64 1 to double
  %r22 = call double @llvm.floor.f64(double %a22)
  %b22 = bitcast double %r22 to i64
  %c22 = icmp eq i64 %b22, 0
  %a23 = bitcast i64 9223372036854775808 to double
  %r23 = call double @llvm.roundeven.f64(double %a23)
  %b23 = bitcast double %r23 to i64
  %c23 = icmp eq i64 %b23, 9223372036854775808
  %a24 = bitcast i64 9218868437227405312 to double
  %r24 = call double @llvm.round.f64(double %a24)
  %b24 = bitcast double %r24 to i64
  %c24 = icmp eq i64 %b24, 9218868437227405312
  %a25 = bitcast i64 18442240474082181120 to double
  %r25 = call double @llvm.trunc.f64(double %a25)
  %b25 = bitcast double %r25 to i64
  %c25 = icmp eq i64 %b25, 18442240474082181120
  %a26 = bitcast i64 9218868437227405313 to double
  %r26 = call double @llvm.ceil.f64(double %a26)
  %b26 = bitcast double %r26 to i64
  %c26 = icmp eq i64 %b26, 9221120237041090561
  %a27 = bitcast i64 4845873199050653696 to double
  %r27 = call double @llvm.floor.f64(double %a27)
  %b27 = bitcast double %r27 to i64
  %c27 = icmp eq i64 %b27, 4845873199050653696
  %vr = call <2 x float> @llvm.ceil.v2f32(<2 x float> <float 1.500000e+00, float -1.500000e+00>)
  %v0 = extractelement <2 x float> %vr, i32 0
  %v1 = extractelement <2 x float> %vr, i32 1
  %vb0 = bitcast float %v0 to i32
  %vb1 = bitcast float %v1 to i32
  %vc0 = icmp eq i32 %vb0, 1073741824
  %vc1 = icmp eq i32 %vb1, 3212836864
  %ok0 = and i1 %vc0, %vc1
  %ok1 = and i1 %ok0, %c0
  %ok2 = and i1 %ok1, %c1
  %ok3 = and i1 %ok2, %c2
  %ok4 = and i1 %ok3, %c3
  %ok5 = and i1 %ok4, %c4
  %ok6 = and i1 %ok5, %c5
  %ok7 = and i1 %ok6, %c6
  %ok8 = and i1 %ok7, %c7
  %ok9 = and i1 %ok8, %c8
  %ok10 = and i1 %ok9, %c9
  %ok11 = and i1 %ok10, %c10
  %ok12 = and i1 %ok11, %c11
  %ok13 = and i1 %ok12, %c12
  %ok14 = and i1 %ok13, %c13
  %ok15 = and i1 %ok14, %c14
  %ok16 = and i1 %ok15, %c15
  %ok17 = and i1 %ok16, %c16
  %ok18 = and i1 %ok17, %c17
  %ok19 = and i1 %ok18, %c18
  %ok20 = and i1 %ok19, %c19
  %ok21 = and i1 %ok20, %c20
  %ok22 = and i1 %ok21, %c21
  %ok23 = and i1 %ok22, %c22
  %ok24 = and i1 %ok23, %c23
  %ok25 = and i1 %ok24, %c24
  %ok26 = and i1 %ok25, %c25
  %ok27 = and i1 %ok26, %c26
  %ok28 = and i1 %ok27, %c27
  %result = select i1 %ok28, i32 71, i32 255
  ret i32 %result
}
