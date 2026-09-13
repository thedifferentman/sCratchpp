static unsigned fib(unsigned n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
int main(void) {
    return (int)fib(12);
}
