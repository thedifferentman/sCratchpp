extern "C" int __cxa_atexit(void (*)(void*), void*, void*);
extern "C" void __cxa_finalize(void*);

static int dso_a, dso_b;
static int one = 1, two = 2, three = 3, five = 5;
static void record(void* value) {
    __asm__ volatile("data_setvariableto VARIABLE=\"lifecycle\" VALUE=(operator_add NUM1=(operator_multiply NUM1=(data_variable VARIABLE=\"lifecycle\") NUM2=10) NUM2=%0)" : : "r"(*static_cast<int*>(value)));
}
static void register_and_reenter(void*) {
    int four = 4;
    record(&four);
    __cxa_atexit(record, &five, &dso_b);
    __cxa_finalize(&dso_b);
}

int main() {
    if (__cxa_atexit(record, &one, &dso_a) || __cxa_atexit(record, &two, &dso_b) ||
        __cxa_atexit(record, &three, &dso_a)) return 1;
    __cxa_finalize(&dso_a);
    __cxa_finalize(&dso_a);
    if (__cxa_atexit(register_and_reenter, nullptr, &dso_b)) return 2;
    __cxa_finalize(&dso_b);
    return 0;
}
