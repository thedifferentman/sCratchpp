#include <scratch_platform.hpp>

namespace scratch {
bool is_turbowarp() {
    bool result;
    asm volatile("argument_reporter_boolean VALUE=\"is turbowarp?\"" : "=r"(result));
    return result;
}
}
