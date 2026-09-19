# Events library

Declare `events = "0.1.0"` under `[dependencies]` in your project manifest, then
include `<events/events.hpp>`. Scrate resolves this ordinary package and the
template links its implementation and event resources. Local development can
use `{path = "../include/events", version = "0.1.0"}` instead. The base SDK does
not include this package automatically. It remains independent of console/PTE.

```cpp
#include <events/events.hpp>

void wheel(int direction, void* context) {
    *static_cast<int*>(context) += direction;
}

int main() {
    int position = 0;
    int token = scratch::events::on_wheel(wheel, &position);
    // In an application's existing loop:
    scratch::events::yield();
    scratch::events::poll();
    scratch::events::off(token);
    return position;
}
```

The compiler generates keyboard hats with small native warp collectors.
Keyboard notifications go to `__scl_events::keyboard`; wheel notifications go
to `__scl_events::wheel`. Both have independently bounded capacity of 128.
The shared diagnostic variable `__scl_events_enabled_dropped` counts discarded
events across both queues when full.

A wheel collector enqueues `+1` (up) or `-1` (down) only if the corresponding physical
arrow key is not currently pressed. It never calls LLVM code or accesses LLVM
stack temporaries. The bounded queue is the Scratch list `__scl_events::wheel`.
The collectors are enabled by the idempotent `init()` (also called by registration
and polling). They only accept events while the program is running.

This wheel detection is a compatibility heuristic: holding an arrow while
scrolling in that direction hides the wheel event; a rapidly released arrow can
be misidentified as a wheel event. Scratch hats also cannot preserve every burst
of raw OS wheel events, and provide direction rather than raw scroll distance.

`on_wheel` and `on_key` share a pool of 16 callbacks and return positive tokens
(0 on failure). `off(token)` unregisters either kind.
Callbacks run serially in the caller of `poll()`. Polling removes a bounded batch
of at most 128 events **from each queue** before any callback runs. It dispatches
the wheel batch followed by the keyboard batch, preserving each queue's order
but not global arrival order across queues. New events and new registrations wait
for the next poll; removing a registration takes effect immediately. Recursive
polling does nothing. `is_dispatching()` lets consumers reject operations that
would recursively wait for events. Callback context objects must outlive their
registration.

Keyboard callbacks take `void(int key, void* context)` and receive key-down
notifications, including OS/browser repetition. They do not receive key-up
events and are not an IME/text-input channel. Printable ASCII keys `32..126`
are supported. In standard Scratch, letters normalize to uppercase (`'a'` and
`'A'` both give `65`). On TurboWarp, the collector samples Shift: pressed means
uppercase, otherwise lowercase. Thus an A-key hat produces `65` with Shift and
`97` without it, regardless of the incoming DOM key's letter case. Caps Lock
is deliberately not modeled. Punctuation follows the key string
reported by the host keyboard layout. Backslash is ordinary ASCII `92`; any
special console editing meaning belongs to the console library.

Named integer constants are available in `scratch::events::key`:

| Constant | Value | Host support |
| --- | ---: | --- |
| `space`, `enter` | 32, 13 | Scratch and TW |
| `left`, `up`, `right`, `down` | `0x110000` through `0x110003` | Scratch and TW |
| `backspace`, `del`, `escape` | 8, 127, 27 | TW |
| `shift`, `caps_lock`, `scroll_lock`, `control` | `0x110004` through `0x110007` | TW |
| `insert`, `home`, `end`, `page_up`, `page_down` | `0x110008` through `0x11000c` | TW |

Standard Scratch ignores the extra TW keys. Tab, function keys and other
unrecognized host keys are not supported. Arrow up/down keyboard collectors
require the physical key currently pressed to filter out wheel-generated hats;
other keyboard collectors do not perform this extra test, so quick releases
still produce notifications. A wheel while holding a matching arrow can produce
another arrow-key notification; a very quick arrow release can be mistaken for
a wheel event. These are limitations of the compatible hat/state technique.

Shift-based casing is guarded by the legacy `is turbowarp?` boolean argument
reporter. It is a standard Scratch opcode; the original VM evaluates the unknown
argument name as false and keeps its ordinary uppercase behavior. No custom
extension, extra sprite, case-lookup costumes or last-key-string bridge is needed.

Shift is sampled when the hat executes, not attached to the original key event.
Releasing Shift before a queued hat runs can make that event lowercase; pressing
Shift before processing can make it uppercase. Scratch may also coalesce repeated
activations of an already running hat. This is intentionally a simple Shift-only
typing model, not a lossless recording of all host keyboard state.

`refresh()` checks a shared native floating-point timestamp and yields after an
8 ms budget, or after a backward clock change. It uses an empty thought for zero
seconds and updates `__scl_refresh_time` after resuming. `yield()` always does so.
Neither function dispatches callbacks. Applications must call these themselves;
there is no automatic yielding in arbitrary user code. A public
`double days_since_2000()` exposes the untruncated clock when needed.

`run()` is an optional explicitly entered loop: poll, yield, repeat until a
callback calls `quit()`. It does not create a background LLVM thread.
