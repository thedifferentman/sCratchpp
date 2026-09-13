target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
@pick = global i1 true
define i32 @indirect(i32 %x) { %y = add i32 %x, 1
ret i32 %y }
define i32 @other(i32 %x) { %y = add i32 %x, 2
ret i32 %y }
define i32 @main() {
  %a = call i32 @indirect(i32 10)
  %choose = load volatile i1, ptr @pick
  %fp = select i1 %choose, ptr @other, ptr @indirect
  %b = call i32 %fp(i32 10)
  %sum = add i32 %a, %b
  %result = sub i32 %sum, 23
  ret i32 %result
}
