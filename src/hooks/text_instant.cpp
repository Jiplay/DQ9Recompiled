// Instant text: holding B while a message box is still printing its text
// prints the rest of the box at once, up to the next page break, wait or
// choice (the game's own stops). See "Instant text" in
// config/hooks/dq9_arm9.toml for the guest code behind the site.
#include "title_hooks.h"

namespace {
// The game's key object (literal at 0x02044418): u16 +0 = keys held,
// u16 +2 = keys held last frame. B is bit 1.
constexpr uint32_t kKeys = 0x02114E30u;
constexpr uint16_t kKeyB = 1u << 1;
}  // namespace

// Before `add r2, r3, r4, lsl #4` at 0x02065D14: r4 is the number of
// characters to print this frame.
NDS_TITLE_HOOK void Dq9TextInstant(uint32_t* chars) {
    if ((bus_read_u16(kKeys) & kKeyB) != 0u) *chars = 0x1000u;
}
