#include "events.hpp"

namespace scratch::events {
namespace {
constexpr int max_handlers = 16;
constexpr int max_batch = 128;
struct Handler {
    WheelHandler callback;
    void* context;
    int token;
    bool keyboard;
};
Handler handlers[max_handlers]{};
bool initialized = false;
bool dispatching = false;
bool running = false;
int next_token = 1;

int allocate_token() {
    // Avoid signed overflow and stale-token collisions after wraparound.
    for (;;) {
        const int candidate = next_token;
        next_token = next_token == 2147483647 ? 1 : next_token + 1;
        bool used = false;
        for (const auto& handler : handlers)
            if (handler.token == candidate) used = true;
        if (!used) return candidate;
    }
}

int subscribe(WheelHandler callback, void* context, bool keyboard) {
    init();
    if (!callback) return 0;
    for (auto& handler : handlers) {
        if (handler.token != 0) continue;
        handler = {callback, context, allocate_token(), keyboard};
        return handler.token;
    }
    return 0;
}
} // namespace

void init() {
    if (initialized) return;
    initialized = true;
    asm volatile(
        "data_deletealloflist LIST=\"__scl_events::wheel\";"
        "data_deletealloflist LIST=\"__scl_events::keyboard\";"
        "data_setvariableto VARIABLE=\"__scl_refresh_time\" VALUE=(sensing_dayssince2000);"
        "data_setvariableto VARIABLE=\"__scl_events_enabled\" VALUE=1"
        : : : "memory");
}

int on_wheel(WheelHandler callback, void* context) {
    return subscribe(callback, context, false);
}

int on_key(KeyHandler callback, void* context) {
    return subscribe(callback, context, true);
}

void off(int token) {
    if (token <= 0) return;
    for (auto& handler : handlers)
        if (handler.token == token) handler = {};
}

void poll() {
    init();
    if (dispatching) return;
    dispatching = true;

    int count, key_count;
    asm volatile("data_lengthoflist LIST=\"__scl_events::wheel\""
                 : "=r"(count) : : "memory");
    asm volatile("data_lengthoflist LIST=\"__scl_events::keyboard\""
                 : "=r"(key_count) : : "memory");
    if (count <= 0 && key_count <= 0) {
        dispatching = false;
        return;
    }
    if (count > max_batch) count = max_batch;
    if (key_count > max_batch) key_count = max_batch;
    int batch[max_batch];
    int key_batch[max_batch];
    for (int i = 0; i < count; ++i) {
        asm volatile(
            "%0=data_itemoflist LIST=\"__scl_events::wheel\" INDEX=1;"
            "data_deleteoflist LIST=\"__scl_events::wheel\" INDEX=1"
            : "=r"(batch[i]) : : "memory");
    }
    for (int i = 0; i < key_count; ++i) {
        asm volatile(
            "%0=data_itemoflist LIST=\"__scl_events::keyboard\" INDEX=1;"
            "data_deleteoflist LIST=\"__scl_events::keyboard\" INDEX=1"
            : "=r"(key_batch[i]) : : "memory");
    }

    int tokens[max_handlers];
    for (int i = 0; i < max_handlers; ++i) tokens[i] = handlers[i].token;
    for (int kind = 0; kind != 2; ++kind) {
        const bool keyboard = kind == 1;
        const int* values = keyboard ? key_batch : batch;
        const int size = keyboard ? key_count : count;
        for (int i = 0; i < size; ++i) {
            if (!keyboard && values[i] != 1 && values[i] != -1) continue;
            for (int h = 0; h < max_handlers; ++h) {
                // A callback may unsubscribe another callback, or replace it.
                // Only the original, still-live registration is called.
                if (tokens[h] == 0 || handlers[h].token != tokens[h] ||
                    handlers[h].keyboard != keyboard) continue;
                const auto callback = handlers[h].callback;
                void* const context = handlers[h].context;
                callback(values[i], context);
            }
        }
    }
    dispatching = false;
}

bool is_dispatching() { return dispatching; }

double days_since_2000() {
    double result;
    asm volatile("sensing_dayssince2000" : "=r"(result) : : "memory");
    return result;
}

void yield() {
    asm volatile(
        "looks_thinkforsecs MESSAGE=\"\" SECS=0;"
        "data_setvariableto VARIABLE=\"__scl_refresh_time\" VALUE=(sensing_dayssince2000)"
        : : : "memory");
}

void refresh() {
    // Sample first, subtract in days, then convert the small difference to ms.
    // The clock never passes through LLVM integer operands or SoftFloat here.
    asm volatile(
        "data_setvariableto VARIABLE=\"__scl_refresh_elapsed\" VALUE="
        "(operator_multiply NUM1=(operator_subtract NUM1=(sensing_dayssince2000) "
        "NUM2=(data_variable VARIABLE=\"__scl_refresh_time\")) NUM2=86400000);"
        "control_if CONDITION=(operator_or OPERAND1="
        "(operator_not OPERAND=(operator_lt OPERAND1="
        "(data_variable VARIABLE=\"__scl_refresh_elapsed\") OPERAND2=8)) "
        "OPERAND2=(operator_lt OPERAND1="
        "(data_variable VARIABLE=\"__scl_refresh_elapsed\") OPERAND2=0)) SUBSTACK %{"
        "looks_thinkforsecs MESSAGE=\"\" SECS=0;"
        "data_setvariableto VARIABLE=\"__scl_refresh_time\" VALUE=(sensing_dayssince2000); %}"
        : : : "memory");
}

void run() {
    init();
    // Do not recursively enter another event loop from a callback.
    if (running || dispatching) return;
    running = true;
    while (running) {
        poll();
        if (running) yield();
    }
}

void quit() { running = false; }
} // namespace scratch::events
