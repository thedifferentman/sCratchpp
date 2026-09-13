; EXPECT: main returns 20. Vector bases, vector indices, and their combination.
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
@a = global [2 x i32] [i32 10, i32 20], align 4
define i32 @main() {
entry:
  %bases0 = insertelement <2 x ptr> poison, ptr @a, i32 0
  %bases = insertelement <2 x ptr> %bases0, ptr @a, i32 1
  %vector_bases = getelementptr i32, <2 x ptr> %bases, i64 1
  %firstptr = extractelement <2 x ptr> %vector_bases, i32 1
  %first = load i32, ptr %firstptr, align 4
  %vector_indices = getelementptr i32, ptr @a, <2 x i64> <i64 0, i64 1>
  %secondptr = extractelement <2 x ptr> %vector_indices, i32 1
  %second = load i32, ptr %secondptr, align 4
  %both = getelementptr i32, <2 x ptr> %bases, <2 x i64> <i64 0, i64 1>
  %thirdptr = extractelement <2 x ptr> %both, i32 1
  %third = load i32, ptr %thirdptr, align 4
  %lowptr = extractelement <2 x ptr> %vector_indices, i32 0
  %low = load i32, ptr %lowptr, align 4
  %aok = icmp eq i32 %first, 20
  %bok = icmp eq i32 %second, 20
  %cok = icmp eq i32 %third, 20
  %lowok = icmp eq i32 %low, 10
  %x = and i1 %aok, %bok
  %y = and i1 %cok, %lowok
  %ok = and i1 %x, %y
  %result = select i1 %ok, i32 %first, i32 999
  ret i32 %result
}
