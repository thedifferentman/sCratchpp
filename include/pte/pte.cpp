#include "pte.hpp"

// The source PTE engine stores one BMP character, two hex width digits,
// then three-digit hex points separated into strokes by M. A point packs
// x in the low six bits and y in the remaining six bits.
// These operations intentionally stay in Scratch's native numeric/string
// domain. The only LLVM values crossing this boundary are integer arguments.
#define PTE_LOOKUP \
    "data_setvariableto VARIABLE=\"__scl_pte_index\" VALUE=" \
    "(data_itemoflist LIST=\"__scl_pte::index\" INDEX=(operator_add NUM1=%0 NUM2=1));" \
    "control_if CONDITION=(operator_not OPERAND=(operator_gt OPERAND1=" \
    "(data_variable VARIABLE=\"__scl_pte_index\") OPERAND2=0)) SUBSTACK %{" \
    "data_setvariableto VARIABLE=\"__scl_pte_index\" VALUE=" \
    "(data_itemoflist LIST=\"__scl_pte::index\" INDEX=9634); %};" \
    "data_setvariableto VARIABLE=\"__scl_pte_code\" VALUE=" \
    "(data_itemoflist LIST=\"__scl_pte::font\" INDEX=" \
    "(data_variable VARIABLE=\"__scl_pte_index\"));"

namespace scratch::pte {
int measure_units(unsigned codepoint) {
    // Use an input-only lookup so the shared template always numbers cp as %0.
    asm volatile(PTE_LOOKUP : : "r"(codepoint) : "memory");
    int width;
    asm volatile(
        "operator_add NUM1=0 NUM2=(operator_join STRING1=\"0x\" STRING2="
        "(operator_join STRING1=(operator_letter_of LETTER=2 STRING="
        "(data_variable VARIABLE=\"__scl_pte_code\")) STRING2="
        "(operator_letter_of LETTER=3 STRING=(data_variable VARIABLE=\"__scl_pte_code\"))))"
        : "=r"(width) : : "memory");
    return width;
}

static void draw_impl(unsigned codepoint, int x, int y, int size, unsigned rgb, int cell_width) {
    asm volatile(
        PTE_LOOKUP
        "data_setvariableto VARIABLE=\"__scl_pte_xscale\" VALUE=%3;"
        "data_setvariableto VARIABLE=\"__scl_pte_originx\" VALUE=%1;"
        "control_if CONDITION=(operator_gt OPERAND1=%5 OPERAND2=0) SUBSTACK %{"
        "data_setvariableto VARIABLE=\"__scl_pte_advance\" VALUE="
        "(operator_add NUM1=0 NUM2=(operator_join STRING1=\"0x\" STRING2="
        "(operator_join STRING1=(operator_letter_of LETTER=2 STRING="
        "(data_variable VARIABLE=\"__scl_pte_code\")) STRING2="
        "(operator_letter_of LETTER=3 STRING=(data_variable VARIABLE=\"__scl_pte_code\")))));"
        "control_if CONDITION=(operator_gt OPERAND1=(operator_divide NUM1="
        "(operator_multiply NUM1=(data_variable VARIABLE=\"__scl_pte_advance\") NUM2=%3) NUM2=64) "
        "OPERAND2=%5) SUBSTACK %{"
        "data_setvariableto VARIABLE=\"__scl_pte_xscale\" VALUE=(operator_divide NUM1="
        "(operator_multiply NUM1=%5 NUM2=64) NUM2=(data_variable VARIABLE=\"__scl_pte_advance\")); %};"
        "data_setvariableto VARIABLE=\"__scl_pte_originx\" VALUE=(operator_add NUM1=%1 NUM2="
        "(operator_divide NUM1=(operator_subtract NUM1=%5 NUM2=(operator_divide NUM1="
        "(operator_multiply NUM1=(data_variable VARIABLE=\"__scl_pte_advance\") NUM2="
        "(data_variable VARIABLE=\"__scl_pte_xscale\")) NUM2=64)) NUM2=2)); %};"
        "pen_penUp;"
        "pen_setPenColorToColor COLOR=%4;"
        "pen_setPenSizeTo SIZE=(operator_divide NUM1=%3 NUM2=16);"
        "data_setvariableto VARIABLE=\"__scl_pte_stroke_start\" VALUE=1;"
        "data_setvariableto VARIABLE=\"__scl_pte_pos\" VALUE=4;"
        "control_repeat_until CONDITION=(operator_gt OPERAND1="
        "(data_variable VARIABLE=\"__scl_pte_pos\") OPERAND2="
        "(operator_length STRING=(data_variable VARIABLE=\"__scl_pte_code\"))) SUBSTACK %{"
        "control_if_else CONDITION=(operator_equals OPERAND1="
        "(operator_letter_of LETTER=(data_variable VARIABLE=\"__scl_pte_pos\") "
        "STRING=(data_variable VARIABLE=\"__scl_pte_code\")) OPERAND2=\"M\") SUBSTACK %{"
        "pen_penUp; data_changevariableby VARIABLE=\"__scl_pte_pos\" VALUE=1;"
        "data_setvariableto VARIABLE=\"__scl_pte_stroke_start\" VALUE=1;"
        "%} SUBSTACK2 %{"
        "data_setvariableto VARIABLE=\"__scl_pte_point\" VALUE="
        "(operator_add NUM1=0 NUM2=(operator_join STRING1=\"0x\" STRING2="
        "(operator_join STRING1=(operator_letter_of LETTER="
        "(data_variable VARIABLE=\"__scl_pte_pos\") STRING="
        "(data_variable VARIABLE=\"__scl_pte_code\")) STRING2="
        "(operator_join STRING1=(operator_letter_of LETTER=(operator_add NUM1="
        "(data_variable VARIABLE=\"__scl_pte_pos\") NUM2=1) STRING="
        "(data_variable VARIABLE=\"__scl_pte_code\")) STRING2="
        "(operator_letter_of LETTER=(operator_add NUM1="
        "(data_variable VARIABLE=\"__scl_pte_pos\") NUM2=2) STRING="
        "(data_variable VARIABLE=\"__scl_pte_code\"))))));"
        "motion_gotoxy X=(operator_add NUM1=(data_variable VARIABLE=\"__scl_pte_originx\") NUM2="
        "(operator_multiply NUM1=(operator_mod NUM1="
        "(data_variable VARIABLE=\"__scl_pte_point\") NUM2=64) NUM2="
        "(operator_divide NUM1=(data_variable VARIABLE=\"__scl_pte_xscale\") NUM2=64))) Y=(operator_subtract NUM1=%2 NUM2="
        "(operator_multiply NUM1=(operator_mathop OPERATOR=\"floor\" NUM="
        "(operator_divide NUM1=(data_variable VARIABLE=\"__scl_pte_point\") NUM2=64)) "
        "NUM2=(operator_divide NUM1=%3 NUM2=64)));"
        "control_if CONDITION=(data_variable VARIABLE=\"__scl_pte_stroke_start\") SUBSTACK %{"
        "pen_penDown; data_setvariableto VARIABLE=\"__scl_pte_stroke_start\" VALUE=0; %};"
        "data_changevariableby VARIABLE=\"__scl_pte_pos\" VALUE=3;"
        "%}; %}; pen_penUp;"
        : : "r"(codepoint), "r"(x), "r"(y), "r"(size), "r"(rgb), "r"(cell_width) : "memory");
}

void draw(unsigned codepoint, int x, int y, int size, unsigned rgb) {
    draw_impl(codepoint, x, y, size, rgb, 0);
}

void draw_cell(unsigned codepoint, int x, int y, int height, int cell_width, unsigned rgb) {
    if (height <= 0 || cell_width <= 0) return;
    draw_impl(codepoint, x, y, height, rgb, cell_width);
}
} // namespace scratch::pte

#undef PTE_LOOKUP
