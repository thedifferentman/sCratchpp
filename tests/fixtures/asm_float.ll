; Native Scratch binary64 reporters bridge directly to LLVM double values.
; This uses bitcasts/comparisons only and needs no SoftFloat runtime.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i32 @main() {
entry:
  %fraction = call double asm sideeffect "operator_divide NUM1=3 NUM2=2", "=r"()
  %negative_zero = call double asm sideeffect "operator_divide NUM1=0 NUM2=-2", "=r"()
  %inf = call double asm sideeffect "operator_divide NUM1=1 NUM2=0", "=r"()
  %nan = call double asm sideeffect "operator_divide NUM1=0 NUM2=0", "=r"()
  %clock = call double asm sideeffect "sensing_dayssince2000", "=r"()
  %a = bitcast double %fraction to i64
  %b = bitcast double %negative_zero to i64
  %c = bitcast double %inf to i64
  %d = bitcast double %nan to i64
  %e = bitcast double %clock to i64
  %ca = icmp eq i64 %a, 4609434218613702656
  %cb = icmp eq i64 %b, -9223372036854775808
  %cc = icmp eq i64 %c, 9218868437227405312
  %cd = icmp eq i64 %d, 9221120237041090560
  %ce = icmp ugt i64 %e, 4607182418800017408
  %ab = and i1 %ca, %cb
  %cd2 = and i1 %cc, %cd
  %abcd = and i1 %ab, %cd2
  %all = and i1 %abcd, %ce
  %result = select i1 %all, i32 73, i32 1
  ret i32 %result
}
