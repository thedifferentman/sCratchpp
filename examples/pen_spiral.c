/* Compile with Clang -S -emit-llvm. The assembly names are official Scratch opcodes. */
int main(void) {
    __asm__ volatile("pen_clear; pen_penUp; motion_gotoxy X=0 Y=0; motion_pointindirection DIRECTION=90; pen_setPenSizeTo SIZE=2; pen_penDown");
    for (int length = 4; length <= 160; length += 4) {
        __asm__ volatile("motion_movesteps STEPS=%0; motion_turnright DEGREES=90" : : "r"(length));
    }
    __asm__ volatile("pen_penUp");
    return 0;
}
