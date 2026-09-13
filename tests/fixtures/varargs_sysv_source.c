/* Reproduce with clang --target=x86_64-unknown-linux-gnu -O1 -S -emit-llvm. */
typedef __builtin_va_list va_list;
typedef unsigned long long U64;
static int read_double(va_list ap) {
    union { double d; U64 u; } bits;
    bits.d = __builtin_va_arg(ap, double);
    return bits.u == 0x3ff0000000000000ULL;
}
__attribute__((noinline)) int pairs(int tag, double named, ...) {
    va_list ap, cp;
    __builtin_va_start(ap, named);
    __builtin_va_copy(cp, ap);
    int sum = tag;
    for (int k = 1; k <= 8; ++k) {
        sum += __builtin_va_arg(ap, int);
        sum += read_double(ap);
    }
    sum += __builtin_va_arg(cp, int) == 1;
    sum += read_double(cp);
    __builtin_va_end(ap);
    __builtin_va_end(cp);
    return sum;
}
__attribute__((noinline)) int named_overflow(
    int a, int b, int c, int d, int e, int f, int g,
    double h, double i, double j, double k, double l,
    double m, double n, double o, double p, ...) {
    va_list ap;
    __builtin_va_start(ap, p);
    int number = __builtin_va_arg(ap, int);
    int real = read_double(ap);
    int *pointer = __builtin_va_arg(ap, int *);
    int result = number + real + *pointer;
    __builtin_va_end(ap);
    return result;
}
int (*volatile indirect)(int, double, ...) = pairs;
int main(void) {
    int value = 17;
    int a = indirect(3, 2.0,
        1, 1.0, 2, 1.0, 3, 1.0, 4, 1.0,
        5, 1.0, 6, 1.0, 7, 1.0, 8, 1.0);
    int b = named_overflow(0,0,0,0,0,0,0, 0.,0.,0.,0.,0.,0.,0.,0.,0.,
                          29, 1.0, &value);
    return a == 49 && b == 47 ? 57 : 1;
}
