; EXPECT: main returns 127 (all seven defined integer boundary checks pass).
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i32 @main() {
entry:
  %carry = add i64 4294967295, 1
  %c0 = icmp eq i64 %carry, 4294967296
  %product = mul i64 4294967295, 4294967295
  %c1 = icmp eq i64 %product, -8589934591
  %quotient = udiv i64 -1, 4294967295
  %c2 = icmp eq i64 %quotient, 4294967297
  %remainder = urem i64 -1, 4294967295
  %c3 = icmp eq i64 %remainder, 0
  %arithmetic = ashr i64 -9223372036854775808, 63
  %c4 = icmp eq i64 %arithmetic, -1
  %odd = add i17 131071, 2
  %c5 = icmp eq i17 %odd, 1
  %signedbool = sext i1 true to i17
  %c6 = icmp eq i17 %signedbool, -1
  %b0 = zext i1 %c0 to i32
  %b1 = select i1 %c1, i32 2, i32 0
  %b2 = select i1 %c2, i32 4, i32 0
  %b3 = select i1 %c3, i32 8, i32 0
  %b4 = select i1 %c4, i32 16, i32 0
  %b5 = select i1 %c5, i32 32, i32 0
  %b6 = select i1 %c6, i32 64, i32 0
  %s1 = or i32 %b0, %b1
  %s2 = or i32 %s1, %b2
  %s3 = or i32 %s2, %b3
  %s4 = or i32 %s3, %b4
  %s5 = or i32 %s4, %b5
  %s6 = or i32 %s5, %b6
  ret i32 %s6
}
