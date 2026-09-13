; Dynamic values prevent constant folding; constant shift counts exercise the
; specialized slot helpers and both orders of power-of-two multiplication.
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"
@negative9 = global i9 -173, align 2
@pattern64 = global i64 9305357566071262703, align 8
@pattern65 = global i65 18446744073709551621, align 16
@pattern257 = global i257 115792089237316195423570985008687907853269984665640564039457665993442346126831, align 16

define i32 @main() {
entry:
  %n = load volatile i9, ptr @negative9, align 2
  %ar1 = ashr i9 %n, 1
  %ar8 = ashr i9 %n, 8
  %lr8 = lshr i9 %n, 8
  %sl1 = shl i9 %n, 1
  %sl8 = shl i9 %n, 8
  %mul9 = mul i9 %n, 128
  %commuted9 = mul i9 128, %n
  %ok.ar1 = icmp eq i9 %ar1, -87
  %ok.ar8 = icmp eq i9 %ar8, -1
  %ok.lr8 = icmp eq i9 %lr8, 1
  %ok.sl1 = icmp eq i9 %sl1, 166
  %ok.sl8 = icmp eq i9 %sl8, 256
  %ok.mul9 = icmp eq i9 %mul9, 384
  %ok.commuted9 = icmp eq i9 %commuted9, 384
  %x = load volatile i64, ptr @pattern64, align 8
  %ar64 = ashr i64 %x, 8
  %lr64 = lshr i64 %x, 8
  %sl64 = shl i64 %x, 8
  %mul64 = mul i64 %x, 256
  %mul64.high = mul i64 -9223372036854775808, %x
  %ok.ar64 = icmp eq i64 %ar64, -35708541045462067
  %ok.lr64 = icmp eq i64 %lr64, 36349052992465869
  %ok.sl64 = icmp eq i64 %sl64, 2541551405711093504
  %ok.mul64 = icmp eq i64 %mul64, 2541551405711093504
  %ok.mul64.high = icmp eq i64 %mul64.high, -9223372036854775808
  %wide = load volatile i65, ptr @pattern65, align 16
  %ar65 = ashr i65 %wide, 64
  %lr65 = lshr i65 %wide, 64
  %sl65 = shl i65 %wide, 64
  %mul65 = mul i65 %wide, 18446744073709551616
  %ok.ar65 = icmp eq i65 %ar65, -1
  %ok.lr65 = icmp eq i65 %lr65, 1
  %ok.sl65 = icmp eq i65 %sl65, 18446744073709551616
  %ok.mul65 = icmp eq i65 %mul65, 18446744073709551616
  %huge = load volatile i257, ptr @pattern257, align 16
  %ar257 = ashr i257 %huge, 255
  %lr257 = lshr i257 %huge, 255
  %sl257 = shl i257 %huge, 256
  %ok.ar257 = icmp eq i257 %ar257, -2
  %ok.lr257 = icmp eq i257 %lr257, 2
  %ok.sl257 = icmp eq i257 %sl257, 115792089237316195423570985008687907853269984665640564039457584007913129639936
  %a = and i1 %ok.ar1, %ok.ar8
  %b = and i1 %ok.lr8, %ok.sl1
  %c = and i1 %ok.sl8, %ok.mul9
  %d = and i1 %ok.commuted9, %ok.ar64
  %e = and i1 %ok.lr64, %ok.sl64
  %f = and i1 %ok.mul64, %ok.mul64.high
  %g = and i1 %ok.ar65, %ok.lr65
  %h = and i1 %ok.sl65, %ok.mul65
  %i = and i1 %ok.ar257, %ok.lr257
  %j = and i1 %a, %b
  %k = and i1 %c, %d
  %l = and i1 %e, %f
  %m = and i1 %g, %h
  %o = and i1 %i, %ok.sl257
  %p = and i1 %j, %k
  %q = and i1 %l, %m
  %r = and i1 %p, %q
  %ok = and i1 %r, %o
  %failure = xor i1 %ok, true
  %result = zext i1 %failure to i32
  ret i32 %result
}
