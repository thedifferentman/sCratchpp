target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"
@single = global float 1.25
@wide = global double 2.5

define i32 @main() {
  %a = load volatile float, ptr @single
  %b = fadd float %a, 0.75
  %i = fptosi float %b to i32
  %x = load volatile double, ptr @wide
  %y = fmul double %x, 4.0
  %j = fptosi double %y to i32
  %result = add i32 %i, %j
  ret i32 %result
}

define i32 @unused_public_function() { ret i32 99 }
