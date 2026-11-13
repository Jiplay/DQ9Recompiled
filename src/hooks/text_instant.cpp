// Instant text: holding B (or the right mouse button, or two fingers on a
// touchscreen, which the frontend reports as it) while a message box is
// still printing its text prints the rest of the box at once, up to the next page break, wait or
// choice (the game's own stops). See "Instant text" in
// config/hooks/dq9_arm9.toml for the guest code behind the site.
#include "title_hooks.h"
#include "title_settings.h"

bool Dq9ChoiceListActive();  // mouse_menus.cpp

namespace {
// The game's key object (literal at 0x02044418): u16 +0 = keys held,
// u16 +2 = keys held last frame. B is bit 1.
constexpr uint32_t kKeys = 0x02114E30u;
constexpr uint16_t kKeyB = 1u << 1;
}  // namespace

namespace {
// The right button as of the last frame a text hook saw it, so a click is
// one advance and holding it while a box prints does not skip the next one.
bool g_right_was_down = false;
}  // namespace

// Before `add r2, r3, r4, lsl #4` at 0x02065D14: r4 is the number of
// characters to print this frame.
NDS_TITLE_HOOK void Dq9TextInstant(uint32_t* chars) {
    const bool right = nds_title_right_mouse_held();
    g_right_was_down = right;
    if ((bus_read_u16(kKeys) & kKeyB) != 0u || right) *chars = 0x1000u;
}

// Before `cmp r0, #0` at 0x02044168: r5 is the "advance" flag the game sets
// for A / B / X. A new right click sets it too, as B does.
// While the manager shows a choice list (state 6 and the list flag at
// +0x19BA, the game's own test) a right click must not answer it. The manager is found through
// the owner literal at 0x020421BC (+28), as in mouse_menus.cpp.
NDS_TITLE_HOOK void Dq9TextAdvanceRightClick(uint32_t* advance) {
    const bool right = nds_title_right_mouse_held();
    const uint32_t owner = bus_read_u32(0x020421BCu);
    const uint32_t mgr = owner != 0 ? bus_read_u32(owner + 28u) : 0u;
    const bool choice = Dq9ChoiceListActive() ||
                        (mgr != 0 && bus_read_u32(mgr + 0x9A0u) == 6u &&
                         bus_read_u8(mgr + 0x19BAu) != 0u);
    if (right && !g_right_was_down && !choice) *advance = 1u;
    g_right_was_down = right;
}
