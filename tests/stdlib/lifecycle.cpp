#include <cstdlib>

static void record(int digit) {
    __asm__ volatile("data_setvariableto VARIABLE=\"lifecycle\" VALUE=(operator_add NUM1=(operator_multiply NUM1=(data_variable VARIABLE=\"lifecycle\") NUM2=10) NUM2=%0)" : : "r"(digit));
}
struct Marker {
    int end;
    Marker(int begin, int finish) : end(finish) { record(begin); }
    ~Marker() { record(end); }
};
static Marker global(1, 9);
static void local() { static Marker once(2, 8); }
static void callback() { record(7); }
static void quick_callback() { record(6); }

int main() {
    local(); local();
    if (std::atexit(callback)) return 1;
    if (std::at_quick_exit(quick_callback)) return 2;
    record(3);
#if MODE == 1
    std::exit(17);
#elif MODE == 2
    std::_Exit(18);
#elif MODE == 3
    std::quick_exit(19);
#else
    return 0;
#endif
}
