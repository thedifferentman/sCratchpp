; EXPECT: main returns 39. All sixteen fcmp predicates on NaNs, unequal values and signed zeros.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i16 @predicate_mask(float %a, float %b) {
entry:
  %c0 = fcmp false float %a, %b
  %z0 = zext i1 %c0 to i16
  %b0 = shl i16 %z0, 0
  %c1 = fcmp oeq float %a, %b
  %z1 = zext i1 %c1 to i16
  %b1 = shl i16 %z1, 1
  %c2 = fcmp ogt float %a, %b
  %z2 = zext i1 %c2 to i16
  %b2 = shl i16 %z2, 2
  %c3 = fcmp oge float %a, %b
  %z3 = zext i1 %c3 to i16
  %b3 = shl i16 %z3, 3
  %c4 = fcmp olt float %a, %b
  %z4 = zext i1 %c4 to i16
  %b4 = shl i16 %z4, 4
  %c5 = fcmp ole float %a, %b
  %z5 = zext i1 %c5 to i16
  %b5 = shl i16 %z5, 5
  %c6 = fcmp one float %a, %b
  %z6 = zext i1 %c6 to i16
  %b6 = shl i16 %z6, 6
  %c7 = fcmp ord float %a, %b
  %z7 = zext i1 %c7 to i16
  %b7 = shl i16 %z7, 7
  %c8 = fcmp uno float %a, %b
  %z8 = zext i1 %c8 to i16
  %b8 = shl i16 %z8, 8
  %c9 = fcmp ueq float %a, %b
  %z9 = zext i1 %c9 to i16
  %b9 = shl i16 %z9, 9
  %c10 = fcmp ugt float %a, %b
  %z10 = zext i1 %c10 to i16
  %b10 = shl i16 %z10, 10
  %c11 = fcmp uge float %a, %b
  %z11 = zext i1 %c11 to i16
  %b11 = shl i16 %z11, 11
  %c12 = fcmp ult float %a, %b
  %z12 = zext i1 %c12 to i16
  %b12 = shl i16 %z12, 12
  %c13 = fcmp ule float %a, %b
  %z13 = zext i1 %c13 to i16
  %b13 = shl i16 %z13, 13
  %c14 = fcmp une float %a, %b
  %z14 = zext i1 %c14 to i16
  %b14 = shl i16 %z14, 14
  %c15 = fcmp true float %a, %b
  %z15 = zext i1 %c15 to i16
  %b15 = shl i16 %z15, 15
  %m1 = or i16 %b0, %b1
  %m2 = or i16 %m1, %b2
  %m3 = or i16 %m2, %b3
  %m4 = or i16 %m3, %b4
  %m5 = or i16 %m4, %b5
  %m6 = or i16 %m5, %b6
  %m7 = or i16 %m6, %b7
  %m8 = or i16 %m7, %b8
  %m9 = or i16 %m8, %b9
  %m10 = or i16 %m9, %b10
  %m11 = or i16 %m10, %b11
  %m12 = or i16 %m11, %b12
  %m13 = or i16 %m12, %b13
  %m14 = or i16 %m13, %b14
  %m15 = or i16 %m14, %b15
  ret i16 %m15
}

define i32 @main() {
entry:
  %nan = bitcast i32 2143289345 to float
  %unordered = call i16 @predicate_mask(float %nan, float 1.000000e+00)
  %larger = call i16 @predicate_mask(float 2.000000e+00, float 1.000000e+00)
  %smaller = call i16 @predicate_mask(float 1.000000e+00, float 2.000000e+00)
  %zeros = call i16 @predicate_mask(float 0.000000e+00, float -0.000000e+00)
  %c0 = icmp eq i16 %unordered, 65280
  %c1 = icmp eq i16 %larger, 52428
  %c2 = icmp eq i16 %smaller, 61680
  %c3 = icmp eq i16 %zeros, 43690
  %p0 = and i1 %c0, %c1
  %p1 = and i1 %c2, %c3
  %ok = and i1 %p0, %p1
  %result = select i1 %ok, i32 39, i32 255
  ret i32 %result
}

