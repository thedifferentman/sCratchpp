#pragma once
#include "blocks.hpp"
#include <set>

namespace scratch {

// Shared, synchronous warp procedures operating directly on __scl_memory.
// D/A are trusted internal byte-slot addresses; P is an internal slot containing
// a guest pointer. Internal copy/fill do not validate object bounds. Guest memory
// operations decode all pointer bytes and validate every nonempty access.
// All copy operations have memmove semantics, including partial overlap. Helpers
// never call guest code; their private scratch variables live only until return.
class SlotMemory {
public:
    explicit SlotMemory(Project& project, unsigned pointer_bytes);
    SlotMemory(Project& project, unsigned pointer_bytes, unsigned data_start, unsigned memory_size);
    void set_bounds(unsigned data_start, unsigned memory_size);

    Stmt copy(Expr destination, Expr source, unsigned bytes);
    Stmt copy(Expr destination, Expr source, Expr bytes);
    Stmt fill(Expr destination, Expr byte, unsigned bytes);
    Stmt fill(Expr destination, Expr byte, Expr bytes);
    Stmt load(Expr destination, Expr pointer_slot, unsigned bytes, unsigned valid_bits = 0);
    Stmt store(Expr pointer_slot, Expr source, unsigned bytes, unsigned valid_bits = 0);

    // Returned expressions refer to distinct caller snapshots and remain valid
    // across later runtime calls. This includes a check of every byte above bit 23.
    Expr decode(Expr pointer_slot, Script& out, const std::string& what = "pointer");
    Expr decode_unsigned(Expr slot, unsigned bytes, Script& out, const std::string& what);
    // Encode a compiler-known nonnegative native integer below 2^24. This is
    // deliberately not a general integer conversion: bytes above byte 2 are zero.
    // The default width is pointer_bytes; narrow destinations retain low bytes.
    Stmt encode(Expr destination, Expr native_value, unsigned bytes = 0);
    Stmt check_access(Expr address, Expr bytes);
    Script trap(const std::string& message);

    // Frame sizes are compiler-known nonnegative counts. Enter checks the heap
    // boundary before changing SP; leave restores the corresponding fixed size.
    Stmt frame_enter(Expr bytes);
    Stmt frame_leave(Expr bytes);
    Stmt stack_restore(Expr pointer_slot, Expr frame_upper);

    // count is an already-decoded, nonnegative integer. A zero count does not
    // decode either guest pointer, check its range, or touch its target.
    Stmt memcpy(Expr destination_pointer, Expr source_pointer, Expr count);
    Stmt memmove(Expr destination_pointer, Expr source_pointer, Expr count);
    Stmt memset(Expr destination_pointer, Expr byte, Expr count);
    Stmt memcpy_slot(Expr destination_pointer, Expr source_pointer, Expr count_slot, unsigned count_bytes);
    Stmt memmove_slot(Expr destination_pointer, Expr source_pointer, Expr count_slot, unsigned count_bytes);
    Stmt memset_slot(Expr destination_pointer, Expr byte, Expr count_slot, unsigned count_bytes);

private:
    Project& project_;
    unsigned pointer_bytes_;
    unsigned serial_ = 0;
    std::set<std::string> emitted_;
    std::string ensure_copy(unsigned bytes);
    std::string ensure_fill(unsigned bytes);
    std::string ensure_decode(unsigned bytes);
    std::string ensure_encode(unsigned bytes);
    std::string ensure_check();
    std::string ensure_trap();
    std::string ensure_access(bool writing, unsigned bytes, unsigned valid_bits);
    std::string ensure_memory(bool filling);
    std::string ensure_memory_slot(bool filling, unsigned count_bytes);
};
}
