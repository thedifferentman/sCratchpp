#pragma once

// Integer arguments map to the current Scratch inline-assembly bridge.
namespace scratch {
inline void clear() { __asm__ volatile("pen_clear"); }
inline void pen_up() { __asm__ volatile("pen_penUp"); }
inline void pen_down() { __asm__ volatile("pen_penDown"); }
inline void go_to(int x, int y) {
    __asm__ volatile("motion_gotoxy X=%0 Y=%1" : : "r"(x), "r"(y));
}
inline void point_in_direction(int degrees) {
    __asm__ volatile("motion_pointindirection DIRECTION=%0" : : "r"(degrees));
}
inline void move(int steps) {
    __asm__ volatile("motion_movesteps STEPS=%0" : : "r"(steps));
}
inline void turn_right(int degrees) {
    __asm__ volatile("motion_turnright DEGREES=%0" : : "r"(degrees));
}
inline void pen_size(int size) {
    __asm__ volatile("pen_setPenSizeTo SIZE=%0" : : "r"(size));
}
} // namespace scratch
