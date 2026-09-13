; Expected exit_code: 0. Out-of-range vector indices produce poison, not an immediate trap; unselected poison cannot affect the result.
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"

define i32 @main() {
entry:
  %bad.element = extractelement <2 x i8> <i8 11, i8 22>, i64 -1
  %selected.element = select i1 false, i8 %bad.element, i8 42
  %bad.vector = insertelement <2 x i8> <i8 11, i8 22>, i8 33, i64 -1
  %selected.vector = select i1 false, <2 x i8> %bad.vector, <2 x i8> <i8 44, i8 55>
  %defined.element = extractelement <2 x i8> %selected.vector, i32 1
  %a = icmp eq i8 %selected.element, 42
  %b = icmp eq i8 %defined.element, 55
  %ok = and i1 %a, %b
  %failure = xor i1 %ok, true
  %result = zext i1 %failure to i32
  ret i32 %result
}
