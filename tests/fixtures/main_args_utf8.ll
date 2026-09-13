; EXPECT: with CLI -- "é", argc=2 and argv[1] contains UTF-8 C3 A9 00; returns 197.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"
define i32 @main(i32 %argc, ptr %argv) {
entry:
  %slot = getelementptr ptr, ptr %argv, i64 1
  %text = load ptr, ptr %slot, align 8
  %first = load i8, ptr %text, align 1
  %secondptr = getelementptr i8, ptr %text, i64 1
  %second = load i8, ptr %secondptr, align 1
  %endptr = getelementptr i8, ptr %text, i64 2
  %end = load i8, ptr %endptr, align 1
  %firstok = icmp eq i8 %first, 195
  %secondok = icmp eq i8 %second, 169
  %endok = icmp eq i8 %end, 0
  %argcok = icmp eq i32 %argc, 2
  %terminator = getelementptr ptr, ptr %argv, i32 %argc
  %null = load ptr, ptr %terminator, align 8
  %nullok = icmp eq ptr %null, null
  %program = load ptr, ptr %argv, align 8
  %programfirst = load i8, ptr %program, align 1
  %programendptr = getelementptr i8, ptr %program, i64 7
  %programend = load i8, ptr %programendptr, align 1
  %programfirstok = icmp eq i8 %programfirst, 112
  %programendok = icmp eq i8 %programend, 0
  %programok = and i1 %programfirstok, %programendok
  %a = and i1 %firstok, %secondok
  %b = and i1 %a, %endok
  %c = and i1 %b, %argcok
  %d = and i1 %c, %nullok
  %ok = and i1 %d, %programok
  %wide = zext i8 %first to i32
  %sum = add i32 %wide, %argc
  %result = select i1 %ok, i32 %sum, i32 999
  ret i32 %result
}
