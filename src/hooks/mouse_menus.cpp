// Mouse-friendly menus (Game > Mouse-friendly menus, [title] mouse_friendly
// in settings.toml): the menu entry under the mouse is highlighted without
// a click, and one click chooses it. On the DS the first tap highlights an
// entry and the second chooses it. Off leaves the game untouched.
//
// Each menu type has its own pen code, so each is hooked on its own. So far:
// the grid menus of overlay 23 (title, file select; see
// config/hooks/dq9_arm9_ov023.toml) and the window lists of the main ARM9
// (camp menu, priest...; "part 2" in config/hooks/dq9_arm9.toml, and their
// green look, part 3, in the overlay manifests) and the simple choice lists
// (Yes/No; "part 4" in config/hooks/dq9_arm9.toml).
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

// The mouse position when it has moved since the last call for the same
// menu (`key`) that returned one. A resting mouse leaves the cursor to the
// keypad. Each menu keeps its own last position, so two menus stepped in
// the same frame both see a move.
bool HoverMoved(uint32_t key, int* x, int* y) {
    struct Last { uint32_t key; int x, y; };
    static Last last[8] = {};
    static unsigned next = 0;
    if (!nds_title_pointer_hover(x, y)) {
        for (Last& l : last) l = {};
        return false;
    }
    Last* slot = nullptr;
    for (Last& l : last)
        if (l.key == key) slot = &l;
    if (slot == nullptr) {
        slot = &last[next++ % 8];
        *slot = {key, -1, -1};
    }
    if (*x == slot->x && *y == slot->y) return false;
    slot->x = *x;
    slot->y = *y;
    return true;
}

// The grid menu's pen step is running on the mouse position, not on a press.
// Set by Dq9MouseMenuGridPress, which every pass through that code runs
// first.
bool g_grid_hover = false;
int g_grid_hover_x = 0, g_grid_hover_y = 0;

// The same for the window lists: the widget whose pen step is running on
// the mouse position (0 when none), set by Dq9MouseMenuListPress.
uint32_t g_list_hover = 0;
int g_list_hover_x = 0, g_list_hover_y = 0;

}  // namespace

// Before `cmp r1, #0` at 0x021F9384: r1 is the press edge. A moved mouse
// with no button held enters the pen code too.
NDS_TITLE_HOOK void Dq9MouseMenuGridPress(uint32_t* press) {
    g_grid_hover = false;
    if (!Enabled() || *press != 0u) return;
    if (!HoverMoved(0u, &g_grid_hover_x, &g_grid_hover_y)) return;
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

// Window lists (camp menu...): see "part 2" in config/hooks/dq9_arm9.toml.

// Before `cmp r0, #0` at 0x0205BC70 (in 0x0205BC3C): r0 is the press edge,
// r4 the widget. A moved mouse with no button held enters the pen code too.
NDS_TITLE_HOOK void Dq9MouseMenuListPress(uint32_t* press, uint32_t* widget) {
    g_list_hover = 0;
    if (!Enabled() || *press != 0u) return;
    if (!HoverMoved(*widget, &g_list_hover_x, &g_list_hover_y)) return;
    g_list_hover = *widget;
    *press = 1u;
}

// At 0x0205BC8C, before the position is loaded: x is at sp+4, y at sp+0.
NDS_TITLE_HOOK void Dq9MouseMenuListPosition(uint32_t* sp) {
    if (g_list_hover == 0) return;
    bus_write_u32(*sp + 4u, static_cast<uint32_t>(g_list_hover_x));
    bus_write_u32(*sp + 0u, static_cast<uint32_t>(g_list_hover_y));
}

// Before `movne r0, #0` at 0x0205BCC4: Z is set when the entry under the pen
// is the one touched before, and only then may the press choose it. A click
// may always choose; the scene still wants it on the cursor entry, and the
// mouse has put the cursor there.
NDS_TITLE_HOOK void Dq9MouseMenuListSame(uint32_t* cpsr) {
    if (Enabled() && g_list_hover == 0) *cpsr |= kCpsrZ;
}

// Before the `beq` that skips a list's green look (part 3, see
// config/hooks/dq9_arm9_ov002.toml): Z is set when the pen is not down. The
// scene only gets here when the list's pen step moved the cursor, and with
// no button held only the mouse hover can have done that, so the entry it
// moved to turns green as it does under the pen.
NDS_TITLE_HOOK void Dq9MouseMenuListGreen(uint32_t* cpsr) {
    if (!Enabled()) return;
    int x = 0, y = 0;
    if (nds_title_pointer_hover(&x, &y)) *cpsr &= ~kCpsrZ;
}

// Before `b 0x0205C6C4` at 0x0205C65C: the pass on the mouse position has
// moved the cursor. Clear the press state it left (w+0x4C..0x4E) so the next
// real press is handled as a fresh one; w+0x30 keeps the entry, which makes
// it "touched before" for the click.
NDS_TITLE_HOOK void Dq9MouseMenuListDone(uint32_t* owner) {
    if (g_list_hover == 0 || g_list_hover != *owner + 4u) return;
    g_list_hover = 0;
    for (uint32_t off = 0x4C; off <= 0x4E; ++off)
        bus_write_u8(*owner + 4u + off, 0u);
}

// Simple choice lists (Yes/No...): see "part 4" in config/hooks/dq9_arm9.toml.

namespace {
// The list (m+0x10) whose hit-test is running on the mouse position (0 when
// none), set by Dq9MouseMenuChoicePress, and whether a real press is being
// handled with the setting on.
uint32_t g_choice_hover = 0;
bool g_choice_click = false;
int g_choice_hover_x = 0, g_choice_hover_y = 0;
}  // namespace

// Before `cmp r1, #0` at 0x020E1E70 (in 0x020E1E5C): r1 is the press edge,
// r4 the list's rects. A moved mouse with no button held enters the pen
// code too.
NDS_TITLE_HOOK void Dq9MouseMenuChoicePress(uint32_t* press, uint32_t* rects) {
    g_choice_hover = 0;
    g_choice_click = false;
    if (!Enabled()) return;
    if (*press != 0u) {
        g_choice_click = true;
        return;
    }
    if (!HoverMoved(*rects, &g_choice_hover_x, &g_choice_hover_y)) return;
    g_choice_hover = *rects;
    *press = 1u;
}

// At 0x020E1E84, after the position is read: x is at sp+4, y at sp+0.
NDS_TITLE_HOOK void Dq9MouseMenuChoicePosition(uint32_t* sp) {
    if (g_choice_hover == 0) return;
    bus_write_u32(*sp + 4u, static_cast<uint32_t>(g_choice_hover_x));
    bus_write_u32(*sp + 0u, static_cast<uint32_t>(g_choice_hover_y));
}

// Before `cmp r2, r0` at 0x020E26CC: r2 is the cursor, r0 the entry under
// the pen, and only when they match may the press choose (the cursor). The
// hover pass must not choose, so it never matches; a click chooses the
// entry under it even when the cursor is elsewhere (the mouse may have
// rested there since the list opened).
NDS_TITLE_HOOK void Dq9MouseMenuChoiceSame(uint32_t* entry, uint32_t* cursor) {
    if (g_choice_hover != 0)
        *cursor = *entry + 1u;
    else if (g_choice_click)
        *cursor = *entry;
}

// Before `cmp r1, #0` at 0x020E26D8: r1 is m+0x17, "touched before". A
// click may choose without it.
NDS_TITLE_HOOK void Dq9MouseMenuChoiceTouched(uint32_t* touched) {
    if (g_choice_click) *touched = 1u;
}

// Layout menus (shop, Patty's party planner, Krak Pot...): see "part 5" in
// config/hooks/dq9_arm9_ov003.toml.

namespace {
// What the next run of the tap routine 0x02080DE4 is for, set by the scene's
// press hook: 0 = leave it alone, 1 = the mouse hover, 2 = a click.
int g_tap_mode = 0;
int g_tap_hover_x = 0, g_tap_hover_y = 0;
}  // namespace

// Before `cmp r1, #0` after the scene reads the press edge (r1). `sp` tells
// the scenes apart for HoverMoved. A moved mouse with no button held enters
// the pen code too.
NDS_TITLE_HOOK void Dq9MouseMenuTapPress(uint32_t* press, uint32_t* sp) {
    g_tap_mode = 0;
    if (!Enabled()) return;
    if (*press != 0u) {
        g_tap_mode = 2;
        return;
    }
    if (!HoverMoved(*sp ^ 0x80000000u, &g_tap_hover_x, &g_tap_hover_y))
        return;
    g_tap_mode = 1;
    *press = 1u;
}

// At the entry of 0x02080DE4: r2 / r3 are the pen position.
NDS_TITLE_HOOK void Dq9MouseMenuTapPosition(uint32_t* x, uint32_t* y) {
    if (g_tap_mode != 1) return;
    *x = static_cast<uint32_t>(g_tap_hover_x);
    *y = static_cast<uint32_t>(g_tap_hover_y);
}

// Before `tst r0, #4` at 0x02080E28: bit 2 of the item's flags means it is
// green already, and only then does the tap choose it. The hover never
// chooses; a click always does.
NDS_TITLE_HOOK void Dq9MouseMenuTapChoose(uint32_t* flags) {
    if (g_tap_mode == 1)
        *flags &= ~4u;
    else if (g_tap_mode == 2)
        *flags |= 4u;
    g_tap_mode = 0;
}
