; EXPECT: main returns 31. byval modifies its copy; sret and aggregate values work.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%Pair = type { i32, i16 }

define { i32, i1 } @make_pair(i32 %x) {
entry:
  %a = insertvalue { i32, i1 } zeroinitializer, i32 %x, 0
  %b = insertvalue { i32, i1 } %a, i1 true, 1
  ret { i32, i1 } %b
}

define i32 @use_copy(ptr byval(%Pair) align 4 %copy) {
entry:
  %old = load i32, ptr %copy, align 4
  store i32 99, ptr %copy, align 4
  ret i32 %old
}

define void @write_pair(ptr sret(%Pair) align 4 %out) {
entry:
  store i32 7, ptr %out, align 4
  %second = getelementptr %Pair, ptr %out, i64 0, i32 1
  store i16 8, ptr %second, align 2
  ret void
}

define i32 @main() {
entry:
  %original = alloca %Pair, align 4
  call void @write_pair(ptr sret(%Pair) align 4 %original)
  %copyvalue = call i32 @use_copy(ptr byval(%Pair) align 4 %original)
  %originalvalue = load i32, ptr %original, align 4
  %pair = call { i32, i1 } @make_pair(i32 16)
  %number = extractvalue { i32, i1 } %pair, 0
  %boolean = extractvalue { i32, i1 } %pair, 1
  %bit = zext i1 %boolean to i32
  %s1 = add i32 %copyvalue, %originalvalue
  %s2 = add i32 %s1, %number
  %result = add i32 %s2, %bit
  ret i32 %result
}
