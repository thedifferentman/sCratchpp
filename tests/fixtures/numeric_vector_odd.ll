; Expected exit_code: 0. Lane boundaries cross byte boundaries; vector storage is not an array of byte-rounded elements.
target datalayout = "e-p:64:64-i64:64-n8:16:32:64-S128"

@packed = global <3 x i5> <i5 31, i5 2, i5 16>, align 2
@objects = global [2 x { i8, i17, <3 x i5> }] [
  { i8, i17, <3 x i5> } { i8 9, i17 131071, <3 x i5> <i5 1, i5 2, i5 3> },
  { i8, i17, <3 x i5> } { i8 7, i17 65537, <3 x i5> <i5 4, i5 5, i5 6> }
], align 4

define i32 @main() {
entry:
  %v = load <3 x i5>, ptr @packed, align 2
  %raw = bitcast <3 x i5> %v to i15
  %ok.raw = icmp eq i15 %raw, 16479
  %sum = add <3 x i5> %v, <i5 1, i5 31, i5 17>
  %modified = insertelement <3 x i5> %sum, i5 31, i32 1
  %selected = select <3 x i1> <i1 true, i1 false, i1 true>, <3 x i5> %modified, <3 x i5> <i5 9, i5 2, i5 9>
  %shuffled = shufflevector <3 x i5> %selected, <3 x i5> %v, <3 x i32> <i32 2, i32 4, i32 0>
  %bits = bitcast <3 x i5> %shuffled to i15
  %ok.vector = icmp eq i15 %bits, 65
  %array.field = getelementptr [2 x { i8, i17, <3 x i5> }], ptr @objects, i32 0, i32 1, i32 1
  %narrow = load i17, ptr %array.field, align 4
  %ok.narrow = icmp eq i17 %narrow, 65537
  %aggregate = load { i8, i17, <3 x i5> }, ptr getelementptr ([2 x { i8, i17, <3 x i5> }], ptr @objects, i32 0, i32 0), align 4
  %aggregate.vector = extractvalue { i8, i17, <3 x i5> } %aggregate, 2
  %last = extractelement <3 x i5> %aggregate.vector, i32 2
  %ok.aggregate = icmp eq i5 %last, 3
  %ok.first = and i1 %ok.raw, %ok.vector
  %ok.second = and i1 %ok.narrow, %ok.aggregate
  %ok = and i1 %ok.first, %ok.second
  %failure = xor i1 %ok, true
  %result = zext i1 %failure to i32
  ret i32 %result
}
