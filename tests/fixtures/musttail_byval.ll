; EXPECT: main returns 2507 even with --memory 4096; caller's object remains 7.
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
%Box = type { i32, i8 }
define i32 @advance(ptr byval(%Box) align 4 %box, i32 %n) {
entry:
  %current = load i32, ptr %box, align 4
  %done = icmp eq i32 %n, 0
  br i1 %done, label %finish, label %step
finish:
  ret i32 %current
step:
  %next = sub i32 %n, 1
  %newvalue = add i32 %current, 1
  store i32 %newvalue, ptr %box, align 4
  %result = musttail call i32 @advance(ptr byval(%Box) align 4 %box, i32 %next)
  ret i32 %result
}
define i32 @main() {
entry:
  %original = alloca %Box, align 4
  store %Box { i32 7, i8 0 }, ptr %original, align 4
  %result = call i32 @advance(ptr byval(%Box) align 4 %original, i32 2500)
  %unchanged = load i32, ptr %original, align 4
  %ok = icmp eq i32 %unchanged, 7
  %final = select i1 %ok, i32 %result, i32 9999
  ret i32 %final
}
