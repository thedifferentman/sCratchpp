; EXPECT: main returns 37. Fused rounding, sqrt, NaNs, signed-zero min/max.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare float @llvm.fma.f32(float, float, float)
declare double @llvm.fma.f64(double, double, double)
declare float @llvm.sqrt.f32(float)
declare double @llvm.sqrt.f64(double)
declare float @llvm.fabs.f32(float)
declare float @llvm.copysign.f32(float, float)
declare float @llvm.minnum.f32(float, float)
declare float @llvm.maxnum.f32(float, float)
declare float @llvm.minimum.f32(float, float)
declare float @llvm.maximum.f32(float, float)
declare double @llvm.minimumnum.f64(double, double)
declare double @llvm.maximumnum.f64(double, double)

define i32 @main() {
entry:
  %a = bitcast i32 1065353217 to float
  %b = bitcast i32 1065353214 to float
  %fused = call float @llvm.fma.f32(float %a, float %b, float -1.000000e+00)
  %fused_bits = bitcast float %fused to i32
  %wide_a = bitcast i64 4607182418800017409 to double
  %wide_b = bitcast i64 4607182418800017406 to double
  %wide_fused = call double @llvm.fma.f64(double %wide_a, double %wide_b, double -1.000000e+00)
  %wide_fused_bits = bitcast double %wide_fused to i64
  %root = call float @llvm.sqrt.f32(float 4.000000e+00)
  %wide_root = call double @llvm.sqrt.f64(double 9.000000e+00)
  %absolute = call float @llvm.fabs.f32(float -2.000000e+00)
  %copied = call float @llvm.copysign.f32(float %absolute, float -0.000000e+00)
  %nan = bitcast i32 2143289345 to float
  %wide_nan = bitcast i64 9221120237041090561 to double
  %min_number = call float @llvm.minnum.f32(float %nan, float 3.000000e+00)
  %max_number = call float @llvm.maxnum.f32(float 3.000000e+00, float %nan)
  %minimum = call float @llvm.minimum.f32(float %nan, float 3.000000e+00)
  %min_zero = call float @llvm.minimum.f32(float 0.000000e+00, float -0.000000e+00)
  %max_zero = call float @llvm.maximum.f32(float -0.000000e+00, float 0.000000e+00)
  %min_zero_bits = bitcast float %min_zero to i32
  %max_zero_bits = bitcast float %max_zero to i32
  %wide_min = call double @llvm.minimumnum.f64(double %wide_nan, double 4.000000e+00)
  %wide_max = call double @llvm.maximumnum.f64(double 4.000000e+00, double %wide_nan)
  %c0 = icmp eq i32 %fused_bits, 2826960896
  %c1 = icmp eq i64 %wide_fused_bits, 13362180094408261632
  %c2 = fcmp oeq float %root, 2.000000e+00
  %c3 = fcmp oeq double %wide_root, 3.000000e+00
  %c4 = fcmp oeq float %copied, -2.000000e+00
  %c5 = fcmp oeq float %min_number, 3.000000e+00
  %c6 = fcmp oeq float %max_number, 3.000000e+00
  %c7 = fcmp uno float %minimum, 0.000000e+00
  %c8 = icmp eq i32 %min_zero_bits, 2147483648
  %c9 = icmp eq i32 %max_zero_bits, 0
  %c10 = fcmp oeq double %wide_min, 4.000000e+00
  %c11 = fcmp oeq double %wide_max, 4.000000e+00
  %p0 = and i1 %c0, %c1
  %p1 = and i1 %c2, %c3
  %p2 = and i1 %c4, %c5
  %p3 = and i1 %c6, %c7
  %p4 = and i1 %c8, %c9
  %p5 = and i1 %c10, %c11
  %q0 = and i1 %p0, %p1
  %q1 = and i1 %p2, %p3
  %q2 = and i1 %p4, %p5
  %q3 = and i1 %q0, %q1
  %ok = and i1 %q3, %q2
  %result = select i1 %ok, i32 37, i32 255
  ret i32 %result
}
