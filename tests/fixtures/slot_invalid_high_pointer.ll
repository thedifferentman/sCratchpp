; Runtime error: the low bytes name a valid object but high bytes are nonzero.
; This is a runtime diagnostic fixture run by slot_runtime_vm.cjs, not e2e.cjs.
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"
@value = global i8 42
define i32 @main() {
entry:
  %base = ptrtoint ptr @value to i64
  %tagged = add i64 %base, 4294967296
  %pointer = inttoptr i64 %tagged to ptr
  %loaded = load volatile i8, ptr %pointer
  %result = zext i8 %loaded to i32
  ret i32 %result
}
