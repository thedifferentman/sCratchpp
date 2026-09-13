; EXPECT: main returns 36. Exact f32/f64 arithmetic and mixed-width conversions.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i32 @main() {
entry:
  %a = fadd float 1.500000e+00, 2.250000e+00
  %b = fsub float %a, 1.000000e+00
  %c = fmul float %b, 2.000000e+00
  %d = fdiv float %c, 2.000000e+00
  %r = frem float %c, 2.000000e+00
  %negative = fneg float %r
  %wide = fpext float %negative to double
  %narrow = fptrunc double %wide to float
  %integer = fptosi float %narrow to i32
  %small = fptoui float %a to i16
  %signed = sitofp i8 -7 to float
  %unsigned = uitofp i8 -1 to double
  %quotient = fdiv double 1.350000e+01, 2.000000e+00
  %remainder = frem double %quotient, 2.000000e+00
  %c0 = fcmp oeq float %d, 2.750000e+00
  %c1 = fcmp oeq float %r, 1.500000e+00
  %c2 = icmp eq i32 %integer, -1
  %c3 = icmp eq i16 %small, 3
  %c4 = fcmp oeq float %signed, -7.000000e+00
  %c5 = fcmp oeq double %unsigned, 2.550000e+02
  %c6 = fcmp oeq double %remainder, 7.500000e-01
  %p0 = and i1 %c0, %c1
  %p1 = and i1 %c2, %c3
  %p2 = and i1 %c4, %c5
  %p3 = and i1 %p0, %p1
  %p4 = and i1 %p2, %c6
  %ok = and i1 %p3, %p4
  %result = select i1 %ok, i32 36, i32 255
  ret i32 %result
}
