target datalayout = "e-p:64:64-i64:64-n8:16:32:64"
define i32 @main() {
entry:
  %a = add i32 %b, 1
  %b = add i32 2, 3
  ret i32 %a
}
