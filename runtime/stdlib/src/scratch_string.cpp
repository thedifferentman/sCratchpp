#include <scratch_string.hpp>

namespace scratch {
namespace {
void append_scalar(unsigned cp) {
    // Scratch's project loader strips backspace even from JSON escapes. There
    // is no official character-from-code opcode: represent it visibly instead.
    if (cp == 8) cp = 0xfffd;
    if (cp < 0x10000) {
        const int index = static_cast<int>(cp + 1 - (cp > 8 ? 1 : 0) - (cp >= 0xe000 ? 2048 : 0));
        asm volatile(
            "data_setvariableto VARIABLE=\"__scl_string\" VALUE="
            "(operator_join STRING1=(data_variable VARIABLE=\"__scl_string\") "
            "STRING2=(operator_letter_of LETTER=%0 STRING="
            "(data_itemoflist LIST=\"__scl_unicode\" INDEX=1)))"
            : : "r"(index) : "memory");
    } else {
        const unsigned scalar = cp - 0x10000;
        const int high_index = static_cast<int>(2 * (scalar >> 10) + 1);
        const int low_index = static_cast<int>(2 * (scalar & 1023) + 2);
        asm volatile(
            "data_setvariableto VARIABLE=\"__scl_string\" VALUE="
            "(operator_join STRING1=(data_variable VARIABLE=\"__scl_string\") "
            "STRING2=(operator_join "
            "STRING1=(operator_letter_of LETTER=%0 STRING="
            "(data_itemoflist LIST=\"__scl_unicode\" INDEX=2)) "
            "STRING2=(operator_letter_of LETTER=%1 STRING="
            "(data_itemoflist LIST=\"__scl_unicode\" INDEX=3))))"
            : : "r"(high_index), "r"(low_index) : "memory");
    }
}
}

void set_string(const std::string& text) {
    asm volatile("data_setvariableto VARIABLE=\"__scl_string\" VALUE=\"\"" : : : "memory");
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    for (std::size_t i = 0; i < text.size();) {
        const unsigned first = bytes[i];
        unsigned cp = first;
        unsigned length = 1;
        unsigned minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { cp = first & 31; length = 2; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { cp = first & 15; length = 3; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { cp = first & 7; length = 4; minimum = 0x10000; }
        else if (first >= 0x80) { append_scalar(0xfffd); ++i; continue; }
        bool valid = length <= text.size() - i;
        for (unsigned j = 1; valid && j < length; ++j) {
            const unsigned next = bytes[i + j];
            if ((next & 0xc0) != 0x80) valid = false;
            else cp = (cp << 6) | (next & 63);
        }
        valid = valid && cp >= minimum && cp <= 0x10ffff && (cp < 0xd800 || cp > 0xdfff);
        append_scalar(valid ? cp : 0xfffd);
        i += valid ? length : 1;
    }
}
}
