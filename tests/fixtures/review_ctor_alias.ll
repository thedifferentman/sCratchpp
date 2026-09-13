; EXPECT: main returns 7. Constructor records may point to function aliases.
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"
@state = global i32 0, align 4
@ctor_alias = alias void (), ptr @actual_ctor
@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [
  { i32, ptr, ptr } { i32 100, ptr @ctor_alias, ptr null }
]
define void @actual_ctor() {
entry:
  store i32 7, ptr @state, align 4
  ret void
}
define i32 @main() {
entry:
  %value = load i32, ptr @state, align 4
  ret i32 %value
}
