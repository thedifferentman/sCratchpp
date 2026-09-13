; EXPECT: main returns 35. Non-byte vector lanes and vector mask packing.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i32 @main() {
entry:
  %sum = add <3 x i3> <i3 7, i3 2, i3 4>, <i3 2, i3 3, i3 6>
  ; sum = [1,5,2].
  %cmp = icmp ult <3 x i3> %sum, <i3 2, i3 4, i3 3>
  %chosen = select <3 x i1> %cmp, <3 x i3> %sum, <3 x i3> <i3 6, i3 6, i3 6>
  ; chosen = [1,6,2].
  %edited = insertelement <3 x i3> %chosen, i3 3, i32 2
  %shuffle = shufflevector <3 x i3> %edited, <3 x i3> zeroinitializer, <3 x i32> <i32 2, i32 1, i32 0>
  %memory = alloca <3 x i3>, align 2
  store <3 x i3> %shuffle, ptr %memory, align 2
  %loaded = load <3 x i3>, ptr %memory, align 2
  %a = extractelement <3 x i3> %loaded, i32 0
  %b = extractelement <3 x i3> %loaded, i32 1
  %c = extractelement <3 x i3> %loaded, i32 2
  %aa = zext i3 %a to i32
  %bb = zext i3 %b to i32
  %cc = zext i3 %c to i32
  %s = add i32 %aa, %bb
  %t = add i32 %s, %cc
  %r = add i32 %t, 25
  ret i32 %r
}
