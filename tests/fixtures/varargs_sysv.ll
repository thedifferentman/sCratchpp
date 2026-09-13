; ModuleID = 'tests/fixtures/varargs_sysv_source.c'
source_filename = "tests/fixtures/varargs_sysv_source.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%struct.__va_list_tag = type { i32, i32, ptr, ptr }

@indirect = dso_local global ptr @pairs, align 8

; Function Attrs: nofree noinline norecurse nosync nounwind uwtable
define dso_local i32 @pairs(i32 noundef %tag, double %named, ...) #0 {
entry:
  %ap = alloca [1 x %struct.__va_list_tag], align 16
  %cp = alloca [1 x %struct.__va_list_tag], align 16
  call void @llvm.lifetime.start.p0(ptr nonnull %ap) #5
  call void @llvm.lifetime.start.p0(ptr nonnull %cp) #5
  call void @llvm.va_start.p0(ptr nonnull %ap)
  call void @llvm.va_copy.p0(ptr nonnull %cp, ptr nonnull %ap)
  %fp_offset_p.i = getelementptr inbounds nuw i8, ptr %ap, i64 4
  %ap.promoted = load i32, ptr %ap, align 16
  %fp_offset_p.i.promoted = load i32, ptr %fp_offset_p.i, align 4
  %overflow_arg_area_p = getelementptr inbounds nuw i8, ptr %ap, i64 8
  %0 = getelementptr inbounds nuw i8, ptr %ap, i64 16
  %reg_save_area = load ptr, ptr %0, align 16
  %overflow_arg_area_p.i = getelementptr inbounds nuw i8, ptr %ap, i64 8
  %overflow_arg_area_p.promoted = load ptr, ptr %overflow_arg_area_p, align 8
  br label %for.body

for.cond.cleanup:                                 ; preds = %read_double.exit
  %gp_offset8 = load i32, ptr %cp, align 16
  %fits_in_gp9 = icmp ult i32 %gp_offset8, 41
  br i1 %fits_in_gp9, label %vaarg.in_reg10, label %vaarg.in_mem12

for.body:                                         ; preds = %entry, %read_double.exit
  %overflow_arg_area50 = phi ptr [ %overflow_arg_area_p.promoted, %entry ], [ %overflow_arg_area48, %read_double.exit ]
  %k.047 = phi i32 [ 1, %entry ], [ %inc, %read_double.exit ]
  %sum.046 = phi i32 [ %tag, %entry ], [ %add5, %read_double.exit ]
  %gp_offset4445 = phi i32 [ %ap.promoted, %entry ], [ %gp_offset43, %read_double.exit ]
  %1 = phi i32 [ %fp_offset_p.i.promoted, %entry ], [ %9, %read_double.exit ]
  %fits_in_gp = icmp ult i32 %gp_offset4445, 41
  br i1 %fits_in_gp, label %vaarg.in_reg, label %vaarg.in_mem

vaarg.in_reg:                                     ; preds = %for.body
  %2 = zext nneg i32 %gp_offset4445 to i64
  %3 = getelementptr i8, ptr %reg_save_area, i64 %2
  %4 = add nuw nsw i32 %gp_offset4445, 8
  store i32 %4, ptr %ap, align 16
  br label %vaarg.end

vaarg.in_mem:                                     ; preds = %for.body
  %overflow_arg_area.next = getelementptr i8, ptr %overflow_arg_area50, i64 8
  store ptr %overflow_arg_area.next, ptr %overflow_arg_area_p, align 8
  br label %vaarg.end

vaarg.end:                                        ; preds = %vaarg.in_mem, %vaarg.in_reg
  %overflow_arg_area49 = phi ptr [ %overflow_arg_area50, %vaarg.in_reg ], [ %overflow_arg_area.next, %vaarg.in_mem ]
  %gp_offset43 = phi i32 [ %4, %vaarg.in_reg ], [ %gp_offset4445, %vaarg.in_mem ]
  %vaarg.addr = phi ptr [ %3, %vaarg.in_reg ], [ %overflow_arg_area50, %vaarg.in_mem ]
  %5 = load i32, ptr %vaarg.addr, align 4, !tbaa !5
  %add = add nsw i32 %5, %sum.046
  %fits_in_fp.i = icmp ult i32 %1, 161
  br i1 %fits_in_fp.i, label %vaarg.in_reg.i, label %vaarg.in_mem.i

vaarg.in_reg.i:                                   ; preds = %vaarg.end
  %6 = zext nneg i32 %1 to i64
  %7 = getelementptr i8, ptr %reg_save_area, i64 %6
  %8 = add nuw nsw i32 %1, 16
  store i32 %8, ptr %fp_offset_p.i, align 4
  br label %read_double.exit

vaarg.in_mem.i:                                   ; preds = %vaarg.end
  %overflow_arg_area.next.i = getelementptr i8, ptr %overflow_arg_area49, i64 8
  store ptr %overflow_arg_area.next.i, ptr %overflow_arg_area_p.i, align 8
  br label %read_double.exit

read_double.exit:                                 ; preds = %vaarg.in_reg.i, %vaarg.in_mem.i
  %overflow_arg_area48 = phi ptr [ %overflow_arg_area49, %vaarg.in_reg.i ], [ %overflow_arg_area.next.i, %vaarg.in_mem.i ]
  %9 = phi i32 [ %8, %vaarg.in_reg.i ], [ %1, %vaarg.in_mem.i ]
  %vaarg.addr.i = phi ptr [ %7, %vaarg.in_reg.i ], [ %overflow_arg_area49, %vaarg.in_mem.i ]
  %10 = load i64, ptr %vaarg.addr.i, align 8, !tbaa !9
  %cmp.i = icmp eq i64 %10, 4607182418800017408
  %conv.i = zext i1 %cmp.i to i32
  %add5 = add nsw i32 %add, %conv.i
  %inc = add nuw nsw i32 %k.047, 1
  %exitcond.not = icmp eq i32 %inc, 9
  br i1 %exitcond.not, label %for.cond.cleanup, label %for.body, !llvm.loop !11

vaarg.in_reg10:                                   ; preds = %for.cond.cleanup
  %11 = getelementptr inbounds nuw i8, ptr %cp, i64 16
  %reg_save_area11 = load ptr, ptr %11, align 16
  %12 = zext nneg i32 %gp_offset8 to i64
  %13 = getelementptr i8, ptr %reg_save_area11, i64 %12
  %14 = add nuw nsw i32 %gp_offset8, 8
  store i32 %14, ptr %cp, align 16
  br label %vaarg.end16

vaarg.in_mem12:                                   ; preds = %for.cond.cleanup
  %overflow_arg_area_p13 = getelementptr inbounds nuw i8, ptr %cp, i64 8
  %overflow_arg_area14 = load ptr, ptr %overflow_arg_area_p13, align 8
  %overflow_arg_area.next15 = getelementptr i8, ptr %overflow_arg_area14, i64 8
  store ptr %overflow_arg_area.next15, ptr %overflow_arg_area_p13, align 8
  br label %vaarg.end16

vaarg.end16:                                      ; preds = %vaarg.in_mem12, %vaarg.in_reg10
  %vaarg.addr17 = phi ptr [ %13, %vaarg.in_reg10 ], [ %overflow_arg_area14, %vaarg.in_mem12 ]
  %15 = load i32, ptr %vaarg.addr17, align 4, !tbaa !5
  %fp_offset_p.i30 = getelementptr inbounds nuw i8, ptr %cp, i64 4
  %fp_offset.i31 = load i32, ptr %fp_offset_p.i30, align 4
  %fits_in_fp.i32 = icmp ult i32 %fp_offset.i31, 161
  br i1 %fits_in_fp.i32, label %vaarg.in_reg.i40, label %vaarg.in_mem.i33

vaarg.in_reg.i40:                                 ; preds = %vaarg.end16
  %16 = getelementptr inbounds nuw i8, ptr %cp, i64 16
  %reg_save_area.i41 = load ptr, ptr %16, align 16
  %17 = zext nneg i32 %fp_offset.i31 to i64
  %18 = getelementptr i8, ptr %reg_save_area.i41, i64 %17
  %19 = add nuw nsw i32 %fp_offset.i31, 16
  store i32 %19, ptr %fp_offset_p.i30, align 4
  br label %read_double.exit42

vaarg.in_mem.i33:                                 ; preds = %vaarg.end16
  %overflow_arg_area_p.i34 = getelementptr inbounds nuw i8, ptr %cp, i64 8
  %overflow_arg_area.i35 = load ptr, ptr %overflow_arg_area_p.i34, align 8
  %overflow_arg_area.next.i36 = getelementptr i8, ptr %overflow_arg_area.i35, i64 8
  store ptr %overflow_arg_area.next.i36, ptr %overflow_arg_area_p.i34, align 8
  br label %read_double.exit42

read_double.exit42:                               ; preds = %vaarg.in_reg.i40, %vaarg.in_mem.i33
  %vaarg.addr.i37 = phi ptr [ %18, %vaarg.in_reg.i40 ], [ %overflow_arg_area.i35, %vaarg.in_mem.i33 ]
  %cmp18 = icmp eq i32 %15, 1
  %conv = zext i1 %cmp18 to i32
  %add19 = add nsw i32 %add5, %conv
  %20 = load i64, ptr %vaarg.addr.i37, align 8, !tbaa !9
  %cmp.i38 = icmp eq i64 %20, 4607182418800017408
  %conv.i39 = zext i1 %cmp.i38 to i32
  %add22 = add nsw i32 %add19, %conv.i39
  call void @llvm.va_end.p0(ptr %ap)
  call void @llvm.va_end.p0(ptr %cp)
  call void @llvm.lifetime.end.p0(ptr nonnull %cp) #5
  call void @llvm.lifetime.end.p0(ptr nonnull %ap) #5
  ret i32 %add22
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(ptr captures(none)) #1

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn
declare void @llvm.va_start.p0(ptr) #2

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn
declare void @llvm.va_copy.p0(ptr, ptr) #2

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(ptr captures(none)) #1

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn
declare void @llvm.va_end.p0(ptr) #2

; Function Attrs: mustprogress nofree noinline norecurse nosync nounwind willreturn uwtable
define dso_local i32 @named_overflow(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f, i32 %g, double %h, double %i, double %j, double %k, double %l, double %m, double %n, double %o, double %p, ...) local_unnamed_addr #3 {
entry:
  %ap = alloca [1 x %struct.__va_list_tag], align 16
  call void @llvm.lifetime.start.p0(ptr nonnull %ap) #5
  call void @llvm.va_start.p0(ptr nonnull %ap)
  %gp_offset = load i32, ptr %ap, align 16
  %fits_in_gp = icmp ult i32 %gp_offset, 41
  br i1 %fits_in_gp, label %vaarg.in_reg, label %vaarg.in_mem

vaarg.in_reg:                                     ; preds = %entry
  %0 = getelementptr inbounds nuw i8, ptr %ap, i64 16
  %reg_save_area = load ptr, ptr %0, align 16
  %1 = zext nneg i32 %gp_offset to i64
  %2 = getelementptr i8, ptr %reg_save_area, i64 %1
  %3 = add nuw nsw i32 %gp_offset, 8
  store i32 %3, ptr %ap, align 16
  br label %vaarg.end

vaarg.in_mem:                                     ; preds = %entry
  %overflow_arg_area_p = getelementptr inbounds nuw i8, ptr %ap, i64 8
  %overflow_arg_area = load ptr, ptr %overflow_arg_area_p, align 8
  %overflow_arg_area.next = getelementptr i8, ptr %overflow_arg_area, i64 8
  store ptr %overflow_arg_area.next, ptr %overflow_arg_area_p, align 8
  br label %vaarg.end

vaarg.end:                                        ; preds = %vaarg.in_mem, %vaarg.in_reg
  %vaarg.addr = phi ptr [ %2, %vaarg.in_reg ], [ %overflow_arg_area, %vaarg.in_mem ]
  %4 = load i32, ptr %vaarg.addr, align 4, !tbaa !5
  %fp_offset_p.i = getelementptr inbounds nuw i8, ptr %ap, i64 4
  %fp_offset.i = load i32, ptr %fp_offset_p.i, align 4
  %fits_in_fp.i = icmp ult i32 %fp_offset.i, 161
  br i1 %fits_in_fp.i, label %vaarg.in_reg.i, label %vaarg.in_mem.i

vaarg.in_reg.i:                                   ; preds = %vaarg.end
  %5 = getelementptr inbounds nuw i8, ptr %ap, i64 16
  %reg_save_area.i = load ptr, ptr %5, align 16
  %6 = zext nneg i32 %fp_offset.i to i64
  %7 = getelementptr i8, ptr %reg_save_area.i, i64 %6
  %8 = add nuw nsw i32 %fp_offset.i, 16
  store i32 %8, ptr %fp_offset_p.i, align 4
  br label %read_double.exit

vaarg.in_mem.i:                                   ; preds = %vaarg.end
  %overflow_arg_area_p.i = getelementptr inbounds nuw i8, ptr %ap, i64 8
  %overflow_arg_area.i = load ptr, ptr %overflow_arg_area_p.i, align 8
  %overflow_arg_area.next.i = getelementptr i8, ptr %overflow_arg_area.i, i64 8
  store ptr %overflow_arg_area.next.i, ptr %overflow_arg_area_p.i, align 8
  br label %read_double.exit

read_double.exit:                                 ; preds = %vaarg.in_reg.i, %vaarg.in_mem.i
  %vaarg.addr.i = phi ptr [ %7, %vaarg.in_reg.i ], [ %overflow_arg_area.i, %vaarg.in_mem.i ]
  %9 = load i64, ptr %vaarg.addr.i, align 8, !tbaa !9
  %gp_offset5 = load i32, ptr %ap, align 16
  %fits_in_gp6 = icmp ult i32 %gp_offset5, 41
  br i1 %fits_in_gp6, label %vaarg.in_reg7, label %vaarg.in_mem9

vaarg.in_reg7:                                    ; preds = %read_double.exit
  %10 = getelementptr inbounds nuw i8, ptr %ap, i64 16
  %reg_save_area8 = load ptr, ptr %10, align 16
  %11 = zext nneg i32 %gp_offset5 to i64
  %12 = getelementptr i8, ptr %reg_save_area8, i64 %11
  %13 = add nuw nsw i32 %gp_offset5, 8
  store i32 %13, ptr %ap, align 16
  br label %vaarg.end13

vaarg.in_mem9:                                    ; preds = %read_double.exit
  %overflow_arg_area_p10 = getelementptr inbounds nuw i8, ptr %ap, i64 8
  %overflow_arg_area11 = load ptr, ptr %overflow_arg_area_p10, align 8
  %overflow_arg_area.next12 = getelementptr i8, ptr %overflow_arg_area11, i64 8
  store ptr %overflow_arg_area.next12, ptr %overflow_arg_area_p10, align 8
  br label %vaarg.end13

vaarg.end13:                                      ; preds = %vaarg.in_mem9, %vaarg.in_reg7
  %vaarg.addr14 = phi ptr [ %12, %vaarg.in_reg7 ], [ %overflow_arg_area11, %vaarg.in_mem9 ]
  %cmp.i = icmp eq i64 %9, 4607182418800017408
  %conv.i = zext i1 %cmp.i to i32
  %14 = load ptr, ptr %vaarg.addr14, align 8, !tbaa !14
  %add = add nsw i32 %4, %conv.i
  %15 = load i32, ptr %14, align 4, !tbaa !5
  %add15 = add nsw i32 %add, %15
  call void @llvm.va_end.p0(ptr %ap)
  call void @llvm.lifetime.end.p0(ptr nonnull %ap) #5
  ret i32 %add15
}

; Function Attrs: nounwind uwtable
define dso_local range(i32 1, 58) i32 @main() local_unnamed_addr #4 {
entry:
  %value = alloca i32, align 4
  call void @llvm.lifetime.start.p0(ptr nonnull %value) #5
  store i32 17, ptr %value, align 4, !tbaa !5
  %0 = load volatile ptr, ptr @indirect, align 8, !tbaa !17
  %call = tail call i32 (i32, double, ...) %0(i32 noundef 3, double noundef 2.000000e+00, i32 noundef 1, double noundef 1.000000e+00, i32 noundef 2, double noundef 1.000000e+00, i32 noundef 3, double noundef 1.000000e+00, i32 noundef 4, double noundef 1.000000e+00, i32 noundef 5, double noundef 1.000000e+00, i32 noundef 6, double noundef 1.000000e+00, i32 noundef 7, double noundef 1.000000e+00, i32 noundef 8, double noundef 1.000000e+00) #5
  %call1 = call i32 (i32, i32, i32, i32, i32, i32, i32, double, double, double, double, double, double, double, double, double, ...) @named_overflow(i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, i32 poison, double poison, double poison, double poison, double poison, double poison, double poison, double poison, double poison, double poison, i32 noundef 29, double noundef 1.000000e+00, ptr noundef nonnull %value)
  %cmp = icmp eq i32 %call, 49
  %cmp2 = icmp eq i32 %call1, 47
  %1 = select i1 %cmp, i1 %cmp2, i1 false
  %cond = select i1 %1, i32 57, i32 1
  call void @llvm.lifetime.end.p0(ptr nonnull %value) #5
  ret i32 %cond
}

attributes #0 = { nofree noinline norecurse nosync nounwind uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { mustprogress nocallback nofree nosync nounwind willreturn }
attributes #3 = { mustprogress nofree noinline norecurse nosync nounwind willreturn uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { nounwind uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #5 = { nounwind }

!llvm.module.flags = !{!0, !1, !2, !3}
!llvm.ident = !{!4}
!llvm.errno.tbaa = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{!"clang version 22.1.8 (https://github.com/llvm/llvm-project ca7933e47d3a3451d81e72ac174dcb5aa28b59d1)"}
!5 = !{!6, !6, i64 0}
!6 = !{!"int", !7, i64 0}
!7 = !{!"omnipotent char", !8, i64 0}
!8 = !{!"Simple C/C++ TBAA"}
!9 = !{!10, !10, i64 0}
!10 = !{!"double", !7, i64 0}
!11 = distinct !{!11, !12, !13}
!12 = !{!"llvm.loop.mustprogress"}
!13 = !{!"llvm.loop.unroll.disable"}
!14 = !{!15, !15, i64 0}
!15 = !{!"p1 int", !16, i64 0}
!16 = !{!"any pointer", !7, i64 0}
!17 = !{!16, !16, i64 0}
