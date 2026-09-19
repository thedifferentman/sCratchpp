#pragma once

namespace scratch::events {
using WheelHandler = void (*)(int direction, void* context);
using KeyHandler = void (*)(int key, void* context);

// Printable keys use ASCII. TurboWarp letters use sampled Shift state (pressed
// => uppercase); Caps Lock is not modeled. Scratch always folds to uppercase.
namespace key {
inline constexpr int space = 32, enter = 13;
inline constexpr int left = 0x110000, up = 0x110001;
inline constexpr int right = 0x110002, down = 0x110003;
inline constexpr int backspace = 8, del = 127, escape = 27;
inline constexpr int shift = 0x110004, caps_lock = 0x110005;
inline constexpr int scroll_lock = 0x110006, control = 0x110007;
inline constexpr int insert = 0x110008, home = 0x110009, end = 0x11000a;
inline constexpr int page_up = 0x11000b, page_down = 0x11000c;
} // namespace key

// Enable the native collectors once. Repeated calls preserve queued events and
// registered handlers. Positive wheel direction means up, negative means down.
void init();

// At most 16 total wheel/key handlers. A positive token, or 0 on failure.
// No callback runs on a Scratch hat thread: poll() dispatches in its caller.
int on_wheel(WheelHandler handler, void* context = nullptr);
// Key-down notifications include host repeat; there are no key-up or IME events.
int on_key(KeyHandler handler, void* context = nullptr);
void off(int token);

// Snapshot at most 128 events per queue, then dispatch wheel followed by key
// events. Recursive poll() does nothing. New events/subscriptions wait until
// the next poll. Arrival order across the two separate queues is not preserved.
void poll();
// True only while poll() is dispatching its snapshot, including inside callbacks.
bool is_dispatching();

// Native floating-point Scratch days since 2000, without integer truncation.
double days_since_2000();

// Let Scratch process its UI and native collectors. Neither function dispatches
// callbacks. refresh() yields only after the shared 8 ms budget has elapsed.
void yield();
void refresh();

// Optional explicitly entered event loop. There is no background LLVM thread.
void run();
void quit();
} // namespace scratch::events
