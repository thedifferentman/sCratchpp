; Packed lane masks and atomic old values remain live across later helpers.
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"
@packed = global <5 x i3> <i3 7, i3 2, i3 4, i3 1, i3 6>, align 2
@index = global i32 3
@counter = global i64 4294967295, align 8
declare <2 x i64> @llvm.masked.gather.v2i64.v2p0(<2 x ptr>, <2 x i1>, <2 x i64>)

define i32 @main() {
entry:
  %v = load volatile <5 x i3>, ptr @packed, align 2
  %sum = add <5 x i3> %v, <i3 2, i3 3, i3 6, i3 7, i3 3>
  %mask = icmp ult <5 x i3> %sum, <i3 2, i3 4, i3 3, i3 1, i3 0>
  %chosen = select <5 x i1> %mask, <5 x i3> %sum, <5 x i3> <i3 6, i3 6, i3 6, i3 6, i3 6>
  %dynamic = load volatile i32, ptr @index
  %edited = insertelement <5 x i3> %chosen, i3 7, i32 %dynamic
  %element = extractelement <5 x i3> %edited, i32 %dynamic
  %bits = bitcast <5 x i3> %edited to i15
  ; [1,6,2,7,6] = 1 + 6*8 + 2*64 + 7*512 + 6*4096.
  %ok.packed = icmp eq i15 %bits, 28337
  %ok.element = icmp eq i3 %element, 7
  %old = atomicrmw add ptr @counter, i64 2 seq_cst, align 8
  %first = cmpxchg ptr @counter, i64 4294967297, i64 8589934593 seq_cst seq_cst, align 8
  %second = cmpxchg ptr @counter, i64 1, i64 2 seq_cst seq_cst, align 8
  %first.old = extractvalue { i64, i1 } %first, 0
  %first.success = extractvalue { i64, i1 } %first, 1
  %second.old = extractvalue { i64, i1 } %second, 0
  %second.success = extractvalue { i64, i1 } %second, 1
  %ok.old = icmp eq i64 %old, 4294967295
  %ok.first = icmp eq i64 %first.old, 4294967297
  %ok.second = icmp eq i64 %second.old, 8589934593
  %failed = xor i1 %second.success, true
  %high = inttoptr i64 4294967296 to ptr
  %p0 = insertelement <2 x ptr> poison, ptr @counter, i32 0
  %p1 = insertelement <2 x ptr> %p0, ptr %high, i32 1
  %gather = call <2 x i64> @llvm.masked.gather.v2i64.v2p0(<2 x ptr> %p1, <2 x i1> <i1 true, i1 false>, <2 x i64> <i64 0, i64 99>)
  %active = extractelement <2 x i64> %gather, i32 0
  %inactive = extractelement <2 x i64> %gather, i32 1
  %ok.active = icmp eq i64 %active, 8589934593
  %ok.inactive = icmp eq i64 %inactive, 99
  %a = and i1 %ok.packed, %ok.element
  %b = and i1 %ok.old, %ok.first
  %c = and i1 %ok.second, %first.success
  %d = and i1 %failed, %ok.active
  %e = and i1 %a, %b
  %f = and i1 %c, %d
  %g = and i1 %e, %f
  %ok = and i1 %g, %ok.inactive
  %failure = xor i1 %ok, true
  %result = zext i1 %failure to i32
  ret i32 %result
}
