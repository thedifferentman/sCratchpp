; EXPECT: main returns 42.
; Full compiler path: named bindings, signed input/output, static symbols,
; nested reporters, command sequences, loops and definite branch outputs.
target datalayout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i32 @main() {
entry:
  %a = call i32 asm "operator_add NUM1=$1 NUM2=$2", "=r,r,r"(i32 -7, i32 2)
  %b = call i32 asm sideeffect "data_setvariableto VARIABLE=\22assembly_counter\22 VALUE=$1; control_repeat TIMES=3 SUBSTACK { data_changevariableby VARIABLE=\22assembly_counter\22 VALUE=2; }; $0 = operator_add NUM1=(data_variable VARIABLE=\22assembly_counter\22) NUM2=(operator_multiply NUM1=3 NUM2=4)", "=r,r,~{memory}"(i32 %a)
  %c = call i1 asm "operator_lt OPERAND1=$1 OPERAND2=$2", "=r,r,r"(i32 -7, i32 2)
  %d = call i32 asm "control_if_else CONDITION=$1 SUBSTACK { $0=operator_add NUM1=-7 NUM2=0; } SUBSTACK2 { $0=operator_add NUM1=7 NUM2=0; }", "=r,r"(i1 %c)
  call void asm sideeffect "", "~{memory}"()
  %aok = icmp eq i32 %a, -5
  %bok = icmp eq i32 %b, 13
  %dok = icmp eq i32 %d, -7
  %ab = and i1 %aok, %bok
  %all = and i1 %ab, %dok
  %result = select i1 %all, i32 42, i32 1
  ret i32 %result
}
