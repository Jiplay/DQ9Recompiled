// Mouse-friendly menus (Game > Mouse-friendly menus, [title] mouse_friendly
// in settings.toml): the menu entry under the mouse is highlighted without
// a click, and one click chooses it. On the DS the first tap highlights an
// entry and the second chooses it. Off leaves the game untouched.
//
// Each menu type has its own pen code, so each is hooked on its own. So far:
// the grid menus of overlay 23 (title, file select); see
// config/hooks/dq9_arm9_ov023.toml for the guest code.
#include "title_hooks.h"
#include "title_settings.h"

namespace {

constexpr const char* kChoices[] = {"Off", "On"};

constexpr NdsTitleSetting kMouseFriendly = {
    "mouse_friendly",
    "Mouse-friendly menus",
    "Highlights the menu entry under the mouse and chooses it with one "
    "click. Off, the first click highlights and the second chooses, as "
    "with the stylus on the DS.",
    0, 1, 1,
    kChoices, sizeof(kChoices) / sizeof(kChoices[0]), nullptr,
    0,
};

const int kMouseFriendlyId = nds_title_setting_register(&kMouseFriendly);

constexpr uint32_t kCpsrZ = 1u << 30;

bool Enabled() { return nds_title_setting_value(kMouseFriendlyId) != 0; }

// The mouse position when it has moved since the last call that returned
// one. A resting mouse leaves the cursor to the keypad.
bool HoverMoved(int* x, int* y) {
    static int last_x = -1, last_y = -1;
    if (!nds_title_pointer_hover(x, y)) {
        last_x = last_y = -1;
        return false;
    }
    if (*x == last_x && *y == last_y) return false;
    last_x = *x;
    last_y = *y;
    return true;
}

// The grid menu's pen step is running on the mouse position, not on a press.
// Set by Dq9MouseMenuGridPress, which every pass through that code runs
// first.
bool g_grid_hover = false;
int g_grid_hover_x = 0, g_grid_hover_y = 0;

}  // namespace

// Before `cmp r1, #0` at 0x021F9384: r1 is the press edge. A moved mouse
// with no button held enters the pen code too.
NDS_TITLE_HOOK void Dq9MouseMenuGridPress(uint32_t* press) {
    g_grid_hover = false;
    if (!Enabled() || *press != 0u) return;
    if (!HoverMoved(&g_grid_hover_x, &g_grid_hover_y)) return;
    g_grid_hover = true;
    *press = 1u;
}

// After the position getter, at 0x021F9398: x is at sp+0x34, y at sp+0x30.
NDS_TITLE_HOOK void Dq9MouseMenuGridPosition(uint32_t* sp) {
    if (!g_grid_hover) return;
    bus_write_u32(*sp + 0x34u, static_cast<uint32_t>(g_grid_hover_x));
    bus_write_u32(*sp + 0x30u, static_cast<uint32_t>(g_grid_hover_y));
}

// Before `movs r6, r0` at 0x021F93AC: r0 is the page arrow under the pen
// (-1, 0 or 1). The mouse turns no pages by passing over an arrow.
NDS_TITLE_HOOK void Dq9MouseMenuGridArrows(uint32_t* arrow) {
    if (g_grid_hover) *arrow = 0u;
}

// Before `b 0x021F9638` at 0x021F9608: the press has just selected the
// cell under the pen (it turned green). A click chooses it as well, through
// the flag at sp+0x28; the cell becomes the cursor on the way.
NDS_TITLE_HOOK void Dq9MouseMenuGridSelected(uint32_t* sp) {
    if (Enabled() && !g_grid_hover) bus_write_u32(*sp + 0x28u, 1u);
}

// Before `moveq r0, #1` at 0x021F961C (plain cells, and selected ones): Z
// is set when the cell under the pen is the cursor, and the press then
// chooses it. A click chooses
// any cell; the mouse passing over chooses none.
NDS_TITLE_HOOK void Dq9MouseMenuGridChoose(uint32_t* cpsr) {
    if (!Enabled()) return;
    if (g_grid_hover)
        *cpsr &= ~kCpsrZ;
    else
        *cpsr |= kCpsrZ;
}
