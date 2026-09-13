; Invalid instructions in unused functions must be rejected before optimization.
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
define internal void @unused() {
entry:
  call void asm sideeffect "pen_penDown; mov eax, eax", ""()
  ret void
}
define i32 @main() {
entry:
  ret i32 0
}
