; EXPECT: main returns 40. Sign operations preserve NaN payloads and need no runtime.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"
declare double @llvm.fabs.f64(double)
declare double @llvm.copysign.f64(double, double)

define i32 @main() {
entry:
  %nan = bitcast i32 2143363909 to float
  %negative = fneg float %nan
  %negative_bits = bitcast float %negative to i32
  %signaling = bitcast i64 18442240474082181121 to double
  %absolute = call double @llvm.fabs.f64(double %signaling)
  %absolute_bits = bitcast double %absolute to i64
  %copied = call double @llvm.copysign.f64(double 0.000000e+00, double -3.000000e+00)
  %copied_bits = bitcast double %copied to i64
  %zero = fneg float -0.000000e+00
  %zero_bits = bitcast float %zero to i32
  %c0 = icmp eq i32 %negative_bits, 4290847557
  %c1 = icmp eq i64 %absolute_bits, 9218868437227405313
  %c2 = icmp eq i64 %copied_bits, 9223372036854775808
  %c3 = icmp eq i32 %zero_bits, 0
  %p0 = and i1 %c0, %c1
  %p1 = and i1 %c2, %c3
  %ok = and i1 %p0, %p1
  %result = select i1 %ok, i32 40, i32 255
  ret i32 %result
}
