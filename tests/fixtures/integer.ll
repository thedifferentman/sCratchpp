; EXPECT: main returns 207
; Covers carry, wrapping, signed division/remainder, shifts and bitwise operations.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i32 @main() {
entry:
  %a = add i8 250, 20
  %b = zext i8 %a to i32
  %c = mul i32 %b, 17
  %d = sdiv i32 -37, 5
  %e = srem i32 -37, 5
  %f = shl i32 %b, 4
  %g = xor i32 %f, 90
  %h = and i32 %g, 255
  %i = or i32 %h, 1
  %j = lshr i32 %i, 1
  %k = add i32 %c, %d
  %l = add i32 %k, %e
  %m = sub i32 %l, %j
  %r = add i32 %m, 71
  ret i32 %r
}
