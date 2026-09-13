; EXPECT: main returns 300. Packed, unaligned, nested array and global relocation.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%Packed = type <{ i8, i32, [3 x i16] }>
@object = global %Packed <{ i8 7, i32 16909060, [3 x i16] [i16 5, i16 258, i16 9] }>, align 1
@fieldptr = global ptr getelementptr (%Packed, ptr @object, i64 0, i32 2, i64 1), align 8
@object_alias = alias %Packed, ptr @object

define i32 @main() {
entry:
  %p = load ptr, ptr @fieldptr, align 8
  %v = load i16, ptr %p, align 1
  %wide = zext i16 %v to i32
  %bytes = getelementptr i8, ptr @object_alias, i64 3
  %byte = load i8, ptr %bytes, align 1
  %small = zext i8 %byte to i32
  %p32 = getelementptr %Packed, ptr @object, i64 0, i32 1
  store i32 40, ptr %p32, align 1
  %n = load i32, ptr %p32, align 1
  %s = add i32 %wide, %small
  %r = add i32 %s, %n
  ret i32 %r
}
