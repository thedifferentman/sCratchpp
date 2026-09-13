; EXPECT: main returns 63. Overflow, bit count, byte swap and saturating boundaries.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"
declare {i32, i1} @llvm.uadd.with.overflow.i32(i32, i32)
declare {i16, i1} @llvm.smul.with.overflow.i16(i16, i16)
declare i32 @llvm.bswap.i32(i32)
declare i64 @llvm.ctpop.i64(i64)
declare i16 @llvm.sadd.sat.i16(i16, i16)
define i32 @main() {
entry:
  %u = call {i32, i1} @llvm.uadd.with.overflow.i32(i32 -1, i32 1)
  %uv = extractvalue {i32, i1} %u, 0
  %uf = extractvalue {i32, i1} %u, 1
  %uz = icmp eq i32 %uv, 0
  %uok = and i1 %uf, %uz
  %s = call {i16, i1} @llvm.smul.with.overflow.i16(i16 300, i16 300)
  %sv = extractvalue {i16, i1} %s, 0
  %sf = extractvalue {i16, i1} %s, 1
  %sz = icmp eq i16 %sv, 24464
  %sok = and i1 %sf, %sz
  %swap = call i32 @llvm.bswap.i32(i32 16909060)
  %swapok = icmp eq i32 %swap, 67305985
  %pop = call i64 @llvm.ctpop.i64(i64 -1)
  %popok = icmp eq i64 %pop, 64
  %sat = call i16 @llvm.sadd.sat.i16(i16 32760, i16 100)
  %satok = icmp eq i16 %sat, 32767
  %normal = call i16 @llvm.sadd.sat.i16(i16 -100, i16 7)
  %normalok = icmp eq i16 %normal, -93
  %a = select i1 %uok, i32 1, i32 0
  %b = select i1 %sok, i32 2, i32 0
  %c = select i1 %swapok, i32 4, i32 0
  %d = select i1 %popok, i32 8, i32 0
  %e = select i1 %satok, i32 16, i32 0
  %f = select i1 %normalok, i32 32, i32 0
  %s1 = or i32 %a, %b
  %s2 = or i32 %s1, %c
  %s3 = or i32 %s2, %d
  %s4 = or i32 %s3, %e
  %r = or i32 %s4, %f
  ret i32 %r
}
