; Expected exit_code: 0. The p0 index width is 32, independent of its 64-bit representation.
target datalayout = "e-p:64:64:64:32-i64:64-n8:16:32:64-S128"

@bytes = global [4 x i8] [i8 11, i8 22, i8 33, i8 44], align 1
@picked = global ptr getelementptr (i8, ptr @bytes, i64 4294967298), align 8

define i32 @main() {
entry:
  %constant.pointer = load ptr, ptr @picked, align 8
  %constant.byte = load i8, ptr %constant.pointer, align 1
  %ok.constant = icmp eq i8 %constant.byte, 33
  %instruction.pointer = getelementptr i8, ptr @bytes, i64 4294967299
  %instruction.byte = load i8, ptr %instruction.pointer, align 1
  %ok.instruction = icmp eq i8 %instruction.byte, 44
  %tagged = inttoptr i64 4294967296 to ptr
  %wrapped = getelementptr i8, ptr %tagged, i64 -1
  %encoded = ptrtoint ptr %wrapped to i64
  %ok.high = icmp eq i64 %encoded, 8589934591
  %ok.first = and i1 %ok.constant, %ok.instruction
  %ok = and i1 %ok.first, %ok.high
  %failure = xor i1 %ok, true
  %result = zext i1 %failure to i32
  ret i32 %result
}
