#include <scratch_string.hpp>
#include <string>

static void switch_name(const std::string& name) {
    scratch::set_string(name);
    asm volatile("looks_switchcostumeto COSTUME=(data_variable VARIABLE=\"__scl_string\")");
}

static int costume_number() {
    int result;
    asm volatile("looks_costumenumbername NUMBER_NAME=\"number\"" : "=r"(result));
    return result;
}

int main() {
    // Actual numbers are deliberately never used to select the costumes.
    switch_name("demo::tile");
    if (costume_number() != 2) return 1;
    asm volatile("looks_show");
    asm volatile("pen_stamp");
    switch_name("demo::missing");
    if (costume_number() != 2) return 2;
    switch_name("demo::copy");
    if (costume_number() != 3) return 3;
    switch_name("demo::123");
    if (costume_number() != 4) return 4;
    switch_name(u8"demo::待机😀");
    if (costume_number() != 5) return 5;
    asm volatile("pen_stamp");
    asm volatile("looks_hide");
    return 0;
}
