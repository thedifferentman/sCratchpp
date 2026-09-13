#include "scratch/slot_memory.hpp"
#include <algorithm>

namespace scratch {
namespace {
constexpr const char* memory = "__scl_memory";
constexpr const char* prefix = "__scl_rt_mem_";
constexpr const char* decoded = "__scl_rt_mem_decoded";
constexpr const char* low_bound = "__scl_rt_mem_data_start";
constexpr const char* high_bound = "__scl_rt_mem_memory_size";

Expr offset(Expr base, unsigned n) { return n ? add(std::move(base), n) : base; }
bool fixed(unsigned bytes) { return bytes == 1 || bytes == 2 || bytes == 4 || bytes == 8; }
std::string suffix(unsigned bytes) { return bytes ? std::to_string(bytes) : "bytes"; }
}

SlotMemory::SlotMemory(Project& project, unsigned pointer_bytes)
    : project_(project), pointer_bytes_(pointer_bytes) {
    if (!pointer_bytes || pointer_bytes > 8) throw Error("unsupported slot-memory pointer width");
}

SlotMemory::SlotMemory(Project& project, unsigned pointer_bytes, unsigned data_start, unsigned memory_size)
    : SlotMemory(project, pointer_bytes) { set_bounds(data_start, memory_size); }

void SlotMemory::set_bounds(unsigned data_start, unsigned memory_size) {
    if (!data_start || data_start > memory_size || memory_size > 200000)
        throw Error("invalid slot-memory address bounds");
    project_.variables[low_bound] = data_start;
    project_.variables[high_bound] = memory_size;
}

std::string SlotMemory::ensure_trap() {
    const std::string name = std::string(prefix) + "trap";
    if (emitted_.insert(name).second)
        project_.procedure(name, {"message"}, {
            set("__scl_status", arg("message")),
            stmt("control_stop", Json::object(), {{"STOP_OPTION", Json::array({"all", nullptr})}})
        });
    return name;
}

Script SlotMemory::trap(const std::string& message) {
    return {call(ensure_trap(), {"error: " + message})};
}

std::string SlotMemory::ensure_copy(unsigned bytes) {
    const std::string name = std::string(prefix) + "copy" + suffix(bytes);
    if (!emitted_.insert(name).second) return name;
    Script body;
    if (bytes) {
        // Snapshot the entire fixed-size source before touching the destination;
        // a partially overlapping transfer is therefore safe in either direction.
        for (unsigned i = 0; i < bytes; ++i)
            body.push_back(set(name + "_b" + std::to_string(i), item(memory, offset(arg("A"), i))));
        for (unsigned i = 0; i < bytes; ++i)
            body.push_back(replace(memory, offset(arg("D"), i), var(name + "_b" + std::to_string(i))));
    } else {
        const auto index = name + "_i";
        const Script forward = {
            set(index, 0),
            repeat(arg("N"), {
                replace(memory, add(arg("D"), var(index)), item(memory, add(arg("A"), var(index)))),
                set(index, add(var(index), 1))
            })
        };
        const Script backward = {
            set(index, arg("N")),
            repeat(arg("N"), {
                set(index, sub(var(index), 1)),
                replace(memory, add(arg("D"), var(index)), item(memory, add(arg("A"), var(index))))
            })
        };
        body.push_back(iff(gt(arg("N"), 0), {
            iff(land(gt(arg("D"), arg("A")), lt(arg("D"), add(arg("A"), arg("N")))), backward, forward)
        }));
    }
    project_.procedure(name, bytes ? std::vector<std::string>{"D", "A"} : std::vector<std::string>{"D", "A", "N"}, std::move(body));
    return name;
}

Stmt SlotMemory::copy(Expr destination, Expr source, unsigned bytes) {
    if (fixed(bytes)) return call(ensure_copy(bytes), {std::move(destination), std::move(source)});
    return copy(std::move(destination), std::move(source), Expr(bytes));
}

Stmt SlotMemory::copy(Expr destination, Expr source, Expr bytes) {
    return call(ensure_copy(0), {std::move(destination), std::move(source), std::move(bytes)});
}

std::string SlotMemory::ensure_fill(unsigned bytes) {
    const std::string name = std::string(prefix) + "fill" + suffix(bytes);
    if (!emitted_.insert(name).second) return name;
    Script body;
    if (bytes) {
        for (unsigned i = 0; i < bytes; ++i)
            body.push_back(replace(memory, offset(arg("D"), i), arg("byte")));
    } else {
        const auto index = name + "_i";
        body = {set(index, 0), repeat(arg("N"), {
            replace(memory, add(arg("D"), var(index)), arg("byte")),
            set(index, add(var(index), 1))
        })};
    }
    project_.procedure(name, bytes ? std::vector<std::string>{"D", "byte"} : std::vector<std::string>{"D", "byte", "N"}, std::move(body));
    return name;
}

Stmt SlotMemory::fill(Expr destination, Expr byte, unsigned bytes) {
    if (fixed(bytes)) return call(ensure_fill(bytes), {std::move(destination), mod(std::move(byte), 256)});
    return fill(std::move(destination), std::move(byte), Expr(bytes));
}

Stmt SlotMemory::fill(Expr destination, Expr byte, Expr bytes) {
    return call(ensure_fill(0), {std::move(destination), mod(std::move(byte), 256), std::move(bytes)});
}

std::string SlotMemory::ensure_decode(unsigned bytes) {
    const std::string name = std::string(prefix) + "decode" + std::to_string(bytes);
    if (!emitted_.insert(name).second) return name;
    Script body;
    if (bytes > 3) {
        // Scan high bytes instead of truncating the value into the address range.
        const auto index = name + "_i";
        body.push_back(set(index, 3));
        body.push_back(repeat(bytes - 3, {
            iff(lnot(eq(item(memory, add(arg("P"), var(index))), 0)), {call(ensure_trap(), {arg("error")})}),
            set(index, add(var(index), 1))
        }));
    }
    Expr value = 0;
    unsigned factor = 1;
    for (unsigned i = 0; i < std::min(bytes, 3u); ++i) {
        auto byte = item(memory, offset(arg("P"), i));
        value = i ? add(std::move(value), mul(std::move(byte), factor)) : byte;
        factor *= 256;
    }
    body.push_back(set(decoded, std::move(value)));
    project_.procedure(name, {"P", "error"}, std::move(body));
    return name;
}

Expr SlotMemory::decode_unsigned(Expr slot, unsigned bytes, Script& out, const std::string& what) {
    const auto result = std::string(prefix) + "value" + std::to_string(serial_++);
    out.push_back(call(ensure_decode(bytes), {std::move(slot), "error: " + what + " exceeds addressable memory"}));
    out.push_back(set(result, var(decoded)));
    return var(result);
}

Expr SlotMemory::decode(Expr pointer_slot, Script& out, const std::string& what) {
    return decode_unsigned(std::move(pointer_slot), pointer_bytes_, out, what);
}

std::string SlotMemory::ensure_encode(unsigned bytes) {
    const std::string name = std::string(prefix) + "encode_small" + std::to_string(bytes);
    if (!emitted_.insert(name).second) return name;
    Script body;
    unsigned factor = 1;
    for (unsigned i = 0; i < std::min(bytes, 3u); ++i) {
        body.push_back(replace(memory, offset(arg("D"), i),
            mod(i ? floor_(scratch::div(arg("value"), factor)) : arg("value"), 256)));
        factor *= 256;
    }
    if (bytes > 3) body.push_back(fill(offset(arg("D"), 3), 0, bytes - 3));
    project_.procedure(name, {"D", "value"}, std::move(body));
    return name;
}

Stmt SlotMemory::encode(Expr destination, Expr native_value, unsigned bytes) {
    // Custom-block arguments are evaluated once on entry, before the first byte
    // is written, so value may safely read from an overlapping destination slot.
    return call(ensure_encode(bytes ? bytes : pointer_bytes_), {std::move(destination), std::move(native_value)});
}

Stmt SlotMemory::frame_enter(Expr bytes) {
    const std::string name = std::string(prefix) + "frame_enter";
    if (emitted_.insert(name).second)
        project_.procedure(name, {"bytes"}, {
            iff(lt(sub(var("__scl_sp"), arg("bytes")), var("__scl_heap")), trap("stack exhausted")),
            set("__scl_sp", sub(var("__scl_sp"), arg("bytes")))
        });
    return call(name, {std::move(bytes)});
}

Stmt SlotMemory::frame_leave(Expr bytes) {
    const std::string name = std::string(prefix) + "frame_leave";
    if (emitted_.insert(name).second)
        project_.procedure(name, {"bytes"}, {set("__scl_sp", add(var("__scl_sp"), arg("bytes")))});
    return call(name, {std::move(bytes)});
}

Stmt SlotMemory::stack_restore(Expr pointer_slot, Expr frame_upper) {
    const std::string name = std::string(prefix) + "stack_restore";
    if (emitted_.insert(name).second)
        project_.procedure(name, {"P", "frame"}, {
            call(ensure_decode(pointer_bytes_), {arg("P"), "error: saved stack pointer exceeds addressable memory"}),
            iff(lor(lt(var(decoded), var("__scl_sp")), gt(var(decoded), arg("frame"))), trap("invalid stack restore")),
            set("__scl_sp", var(decoded))
        });
    return call(name, {std::move(pointer_slot), std::move(frame_upper)});
}

std::string SlotMemory::ensure_check() {
    const std::string name = std::string(prefix) + "check";
    if (!emitted_.insert(name).second) return name;
    const auto bad_address = lor(lt(arg("address"), var(low_bound)), gt(arg("address"), var(high_bound)));
    const auto bad_end = gt(arg("N"), add(sub(var(high_bound), arg("address")), 1));
    project_.procedure(name, {"address", "N"}, {
        iff(gt(arg("N"), 0), {iff(lor(bad_address, bad_end), trap("invalid memory access"))})
    });
    return name;
}

Stmt SlotMemory::check_access(Expr address, Expr bytes) {
    return call(ensure_check(), {std::move(address), std::move(bytes)});
}

std::string SlotMemory::ensure_access(bool writing, unsigned bytes, unsigned valid_bits) {
    if (valid_bits > bytes * 8u || (valid_bits && (valid_bits + 7u) / 8u != bytes))
        throw Error("invalid slot-memory scalar width");
    const unsigned top_bits = valid_bits % 8;
    const std::string name = std::string(prefix) + (writing ? "store" : "load") + std::to_string(bytes) + (top_bits ? "_i" + std::to_string(valid_bits) : "");
    if (!emitted_.insert(name).second) return name;
    Script body;
    if (bytes) {
        body.push_back(call(ensure_decode(pointer_bytes_), {arg("P"), "error: pointer exceeds addressable memory"}));
        body.push_back(check_access(var(decoded), bytes));
        // The leaf copy/fill helpers do not touch decoded. Store the resolved
        // address nonetheless so future leaf implementations can remain private.
        const auto address = name + "_address";
        body.push_back(set(address, var(decoded)));
        if (writing && top_bits) {
            // Canonicalize before the actual store. Rewriting the destination's
            // top byte afterwards would perform two guest stores (observable for
            // volatile accesses). Snapshot first to retain partial-overlap safety.
            const auto top_value = name + "_top";
            body.push_back(set(top_value, mod(item(memory, offset(arg("A"), bytes - 1)), 1u << top_bits)));
            if (bytes > 1) body.push_back(copy(var(address), arg("A"), bytes - 1));
            body.push_back(replace(memory, offset(var(address), bytes - 1), var(top_value)));
        } else if (writing) body.push_back(copy(var(address), arg("A"), bytes));
        else body.push_back(copy(arg("D"), var(address), bytes));
        if (!writing && top_bits) {
            auto top = offset(arg("D"), bytes - 1);
            body.push_back(replace(memory, top, mod(item(memory, top), 1u << top_bits)));
        }
    }
    project_.procedure(name, writing ? std::vector<std::string>{"P", "A"} : std::vector<std::string>{"D", "P"}, std::move(body));
    return name;
}

Stmt SlotMemory::load(Expr destination, Expr pointer_slot, unsigned bytes, unsigned valid_bits) {
    return call(ensure_access(false, bytes, valid_bits), {std::move(destination), std::move(pointer_slot)});
}

Stmt SlotMemory::store(Expr pointer_slot, Expr source, unsigned bytes, unsigned valid_bits) {
    return call(ensure_access(true, bytes, valid_bits), {std::move(pointer_slot), std::move(source)});
}

std::string SlotMemory::ensure_memory(bool filling) {
    const std::string name = std::string(prefix) + (filling ? "memset" : "memmove");
    if (!emitted_.insert(name).second) return name;
    const auto destination = name + "_destination";
    const auto source = name + "_source";
    Script access = {
        call(ensure_decode(pointer_bytes_), {arg("Pdst"), "error: pointer exceeds addressable memory"}),
        set(destination, var(decoded)),
        check_access(var(destination), arg("N"))
    };
    if (filling) access.push_back(fill(var(destination), arg("byte"), arg("N")));
    else {
        access.push_back(call(ensure_decode(pointer_bytes_), {arg("Psrc"), "error: pointer exceeds addressable memory"}));
        access.push_back(set(source, var(decoded)));
        access.push_back(check_access(var(source), arg("N")));
        access.push_back(copy(var(destination), var(source), arg("N")));
    }
    project_.procedure(name, filling ? std::vector<std::string>{"Pdst", "byte", "N"} : std::vector<std::string>{"Pdst", "Psrc", "N"}, {
        iff(gt(arg("N"), 0), std::move(access))
    });
    return name;
}

Stmt SlotMemory::memcpy(Expr destination_pointer, Expr source_pointer, Expr count) {
    // memcpy's source/destination overlap is LLVM UB. Sharing the memmove leaf
    // preserves every defined memcpy behavior while avoiding another procedure.
    return memmove(std::move(destination_pointer), std::move(source_pointer), std::move(count));
}

Stmt SlotMemory::memmove(Expr destination_pointer, Expr source_pointer, Expr count) {
    return call(ensure_memory(false), {std::move(destination_pointer), std::move(source_pointer), std::move(count)});
}

Stmt SlotMemory::memset(Expr destination_pointer, Expr byte, Expr count) {
    return call(ensure_memory(true), {std::move(destination_pointer), mod(std::move(byte), 256), std::move(count)});
}

std::string SlotMemory::ensure_memory_slot(bool filling, unsigned count_bytes) {
    const std::string name = std::string(prefix) + (filling ? "memset_count" : "memmove_count") + std::to_string(count_bytes);
    if (!emitted_.insert(name).second) return name;
    project_.procedure(name, filling ? std::vector<std::string>{"Pdst", "byte", "count"} : std::vector<std::string>{"Pdst", "Psrc", "count"}, {
        call(ensure_decode(count_bytes), {arg("count"), "error: memory size exceeds addressable memory"}),
        call(ensure_memory(filling), {arg("Pdst"), arg(filling ? "byte" : "Psrc"), var(decoded)})
    });
    return name;
}

Stmt SlotMemory::memcpy_slot(Expr destination_pointer, Expr source_pointer, Expr count_slot, unsigned count_bytes) {
    return memmove_slot(std::move(destination_pointer), std::move(source_pointer), std::move(count_slot), count_bytes);
}

Stmt SlotMemory::memmove_slot(Expr destination_pointer, Expr source_pointer, Expr count_slot, unsigned count_bytes) {
    return call(ensure_memory_slot(false, count_bytes), {std::move(destination_pointer), std::move(source_pointer), std::move(count_slot)});
}

Stmt SlotMemory::memset_slot(Expr destination_pointer, Expr byte, Expr count_slot, unsigned count_bytes) {
    return call(ensure_memory_slot(true, count_bytes), {std::move(destination_pointer), mod(std::move(byte), 256), std::move(count_slot)});
}
}
