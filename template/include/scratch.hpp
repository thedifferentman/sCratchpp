#pragma once

#include <string>

// Definitions are generated in build/generated/scratch.cpp from sCrpp.toml.
// Every operation acts on the single Program sprite.
namespace scratch {
// TurboWarp's compatible "is turbowarp?" reporter; false in original Scratch.
bool is_turbowarp();
void clear();
void pen_up();
void pen_down();
void go_to(int x, int y);
void point_in_direction(int degrees);
void move(int steps);
void turn_right(int degrees);
void pen_size(int size);
void show();
void hide();
void stamp();

// Write UTF-8 text to the Scratch variable __scl_string (provided by the SDK).
void set_string(const std::string& text);
// Prefix the current project's package name, then switch the Program costume.
void set_costume(const std::string& name);
// Switch by full "package::name", including independently linked packages.
void set_costume_qualified(const std::string& name);
} // namespace scratch
