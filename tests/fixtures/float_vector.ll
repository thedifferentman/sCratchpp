; EXPECT: main returns 38. Fixed vector arithmetic, packed comparisons and casts.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i32 @main() {
entry:
  %sum = fadd <2 x float> <float 1.500000e+00, float -2.000000e+00>, <float 2.500000e+00, float 5.000000e-01>
  %comparison = fcmp oge <2 x float> %sum, <float 4.000000e+00, float -1.000000e+00>
  %converted = fptosi <2 x float> %sum to <2 x i16>
  %selected = select <2 x i1> %comparison, <2 x i16> %converted, <2 x i16> <i16 9, i16 9>
  %a = extractelement <2 x i16> %selected, i32 0
  %b = extractelement <2 x i16> %selected, i32 1
  %aa = zext i16 %a to i32
  %bb = zext i16 %b to i32
  %ints = sitofp <2 x i16> <i16 -3, i16 4> to <2 x float>
  %extended = fpext <2 x float> %ints to <2 x double>
  %truncated = fptrunc <2 x double> %extended to <2 x float>
  %negative = extractelement <2 x float> %truncated, i32 0
  %correct = fcmp oeq float %negative, -3.000000e+00
  %s = add i32 %aa, %bb
  %t = add i32 %s, 25
  %result = select i1 %correct, i32 %t, i32 255
  ret i32 %result
}
