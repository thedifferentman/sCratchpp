// Integration fixture: compile and link include/events/events.cpp together with
// its sCrpp.toml, then run the SB3 in both Scratch and TurboWarp VMs. Exit 0 passes.
#include <events/events.hpp>

namespace {
int first_total = 0;
int removed_total = 0;
int replacement_total = 0;
int removed_token = 0;
int replacement_token = 0;
int key_total = 0;
int dispatch_errors = 0;

void enqueue_key(int key) {
    asm volatile("data_addtolist LIST=\"__scl_events::keyboard\" ITEM=%0"
                 : : "r"(key) : "memory");
}

void enqueue(int direction) {
    asm volatile("data_addtolist LIST=\"__scl_events::wheel\" ITEM=%0"
                 : : "r"(direction) : "memory");
}
int pending() {
    int count;
    asm volatile("data_lengthoflist LIST=\"__scl_events::wheel\""
                 : "=r"(count) : : "memory");
    return count;
}
void accumulate(int direction, void* value) {
    if (!scratch::events::is_dispatching()) ++dispatch_errors;
    *static_cast<int*>(value) += direction;
}
void first(int direction, void*) {
    first_total += direction;
    if (first_total == 1) {
        scratch::events::off(removed_token);
        replacement_token = scratch::events::on_wheel(accumulate, &replacement_total);
        enqueue(-1);
        enqueue_key('B'); // Must wait for next poll even though keys dispatch last.
        scratch::events::poll(); // Must not reenter or consume the new event.
    }
}
void finish(int, void*) { scratch::events::quit(); }
} // namespace

int main() {
    using namespace scratch::events;
    init();
    if (is_dispatching()) return 8;
    int first_token = on_wheel(first);
    removed_token = on_wheel(accumulate, &removed_total);
    const int key_token = on_key(accumulate, &key_total);
    if (!first_token || !removed_token || !key_token || on_wheel(nullptr) || on_key(nullptr)) return 1;
    enqueue(1);
    enqueue_key('A');
    enqueue(1);
    init(); // Must preserve handlers and pending events.
    poll();
    if (first_total != 2 || removed_total || replacement_total || pending() != 1 || key_total != 'A') return 2;
    off(first_token);
    poll();
    if (replacement_total != -1 || pending() != 0 || key_total != 'A' + 'B') return 3;
    off(replacement_token);
    off(key_token);

    int tokens[16];
    for (int i = 0; i < 16; ++i) {
        tokens[i] = i % 2 ? on_wheel(accumulate, &removed_total) : on_key(accumulate, &key_total);
        if (!tokens[i]) return 4;
    }
    if (on_wheel(accumulate, &removed_total) || on_key(accumulate, &key_total)) return 5;
    for (int token : tokens) off(token);
    off(-1);
    off(0);
    off(removed_token); // A stale token must not unregister a replacement.

    int stop_token = on_wheel(finish);
    enqueue(1);
    run(); // Callback requests exit in the same LLVM execution context.
    off(stop_token);
    if (pending()) return 6;
    yield();
    refresh();
    if (is_dispatching() || dispatch_errors) return 9;
    return days_since_2000() > 9000.0 ? 0 : 7;
}
