target datalayout = "e-p:64:64-i64:64-n8:16:32:64"
target triple = "x86_64-unknown-linux-gnu"
define i32 @main() {
entry:
  call void asm sideeffect "nop", "~{memory}"()
  ret i32 0
}
