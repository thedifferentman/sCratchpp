; EXPECT-ERROR: floating lowering supports IEEE float/double (32/64 bits)
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"
define i32 @main() {
entry:
  %value = fadd half 0xH3C00, 0xH4000
  %result = fptosi half %value to i32
  ret i32 %result
}
