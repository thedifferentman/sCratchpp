; EXPECT: 58. Explicit va_arg, independent va_copy, float named argument,
; narrow integer widths, pointer, float/double bit patterns, recursive call.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"
%va = type { i32, i32, ptr, ptr }
@data = global i32 71
declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_copy.p0.p0(ptr, ptr)
declare void @llvm.va_end.p0(ptr)
define i32 @consume(float %named, i32 %depth, ...) {
entry:
  %ap = alloca %va, align 8
  %cp = alloca %va, align 8
  call void @llvm.va_start.p0(ptr %ap)
  call void @llvm.va_copy.p0.p0(ptr %cp, ptr %ap)
  %recurse = icmp ne i32 %depth, 0
  br i1 %recurse, label %nested, label %values
nested:
  %next = sub i32 %depth, 1
  %inner = call i32 (float, i32, ...) @consume(float 2.0, i32 %next,
      i8 -2, i17 100003, i64 1234605616436508552, ptr @data,
      float 1.0, double 1.0)
  br label %values
values:
  %nested_result = phi i32 [58, %entry], [%inner, %nested]
  %a = va_arg ptr %ap, i8
  %b = va_arg ptr %ap, i17
  %c = va_arg ptr %ap, i64
  %p = va_arg ptr %ap, ptr
  %f = va_arg ptr %ap, float
  %d = va_arg ptr %ap, double
  %a2 = va_arg ptr %cp, i8
  %fb = bitcast float %f to i32
  %db = bitcast double %d to i64
  %ok0 = icmp eq i8 %a, -2
  %ok1 = icmp eq i17 %b, 100003
  %ok2 = icmp eq i64 %c, 1234605616436508552
  %ok3 = icmp eq ptr %p, @data
  %ok4 = icmp eq i32 %fb, 1065353216
  %ok5 = icmp eq i64 %db, 4607182418800017408
  %ok6 = icmp eq i8 %a2, -2
  %x0 = and i1 %ok0, %ok1
  %x1 = and i1 %ok2, %ok3
  %x2 = and i1 %ok4, %ok5
  %x3 = and i1 %x0, %x1
  %x4 = and i1 %x2, %ok6
  %values_ok = and i1 %x3, %x4
  %nested_ok = icmp eq i32 %nested_result, 58
  %ok = and i1 %values_ok, %nested_ok
  call void @llvm.va_end.p0(ptr %ap)
  call void @llvm.va_end.p0(ptr %cp)
  %result = select i1 %ok, i32 58, i32 1
  ret i32 %result
}
define i32 @main() {
  %result = call i32 (float, i32, ...) @consume(float 2.0, i32 2,
      i8 -2, i17 100003, i64 1234605616436508552, ptr @data,
      float 1.0, double 1.0)
  ret i32 %result
}
