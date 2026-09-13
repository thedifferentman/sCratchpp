; EXPECT: main returns 37; stackrestore cannot overwrite live SSA state.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"
declare ptr @llvm.stacksave.p0()
declare void @llvm.stackrestore.p0(ptr)

define i32 @read_and_restore(i32 %n) {
entry:
  %persistent = alloca i32, align 4
  store i32 17, ptr %persistent, align 4
  %saved = call ptr @llvm.stacksave.p0()
  %dynamic = alloca i8, i32 %n, align 16
  store i8 20, ptr %dynamic, align 1
  %v = load i8, ptr %dynamic, align 1
  %live = zext i8 %v to i32
  call void @llvm.stackrestore.p0(ptr %saved)
  %replacement = alloca i8, i32 %n, align 16
  store i8 99, ptr %replacement, align 1
  %old = load i32, ptr %persistent, align 4
  %r = add i32 %live, %old
  ret i32 %r
}

define i32 @main() {
entry:
  %r = call i32 @read_and_restore(i32 23)
  ret i32 %r
}
