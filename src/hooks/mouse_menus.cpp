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
#include <cstdio>

#include "dq9_scene.h"
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

// Character creation: see config/hooks/dq9_arm9_ov009.toml.
//
// Off for now: the hover applies the entry under the mouse at once (gender,
// looks), as the keypad cursor does, which may feel too eager, so the screen
// stays vanilla. Set kCharHover to true to bring it back.

namespace {
constexpr bool kCharHover = false;
bool g_char_hover = false;
constexpr uint32_t kCharPage = 0xC58u;  // u8, page of the menu
}  // namespace

// Before `cmp r1, #0` at 0x02188F24: r1 is the press edge, r4 the menu, and
// the pen position is already at sp+4 / sp+0. A moved mouse with no button
// held takes the press path on the mouse position.
NDS_TITLE_HOOK void Dq9MouseMenuCharPress(uint32_t* press, uint32_t* cc,
                                          uint32_t* sp) {
    g_char_hover = false;
    if (!kCharHover || !Enabled() || *press != 0u) return;
    if (bus_read_u8(*cc + kCharPage) == 8u) return;
    int x = 0, y = 0;
    if (!HoverMoved(*cc, &x, &y)) return;
    bus_write_u32(*sp + 4u, static_cast<uint32_t>(x));
    bus_write_u32(*sp + 0u, static_cast<uint32_t>(y));
    g_char_hover = true;
    *press = 1u;
}

// Before `add r0, r6, #3` at 0x02188F44: r6 is what the pen hit. The hover
// keeps entries (0..100) and drops the tabs and the buttons.
NDS_TITLE_HOOK void Dq9MouseMenuCharHit(uint32_t* hit) {
    if (!g_char_hover) return;
    g_char_hover = false;
    const int32_t h = static_cast<int32_t>(*hit);
    if (h < 0 || h > 100) *hit = static_cast<uint32_t>(-1);
}

// Name keyboard: see "part 7" in config/hooks/dq9_arm9_ov003.toml.

namespace {
bool g_key_hover = false;
int g_key_hover_x = 0, g_key_hover_y = 0;
}  // namespace

// Before `cmp r1, #0` at 0x0215F350: r1 is the press edge, r10 the keyboard.
// A moved mouse with no button held enters the key search too.
NDS_TITLE_HOOK void Dq9MouseMenuKeyPress(uint32_t* press, uint32_t* kb) {
    g_key_hover = false;
    if (!Enabled() || *press != 0u) return;
    if (!HoverMoved(*kb, &g_key_hover_x, &g_key_hover_y)) return;
    g_key_hover = true;
    *press = 1u;
}

// At 0x0215F364, after the pen position is read: x at sp+8, y at sp+0xC.
NDS_TITLE_HOOK void Dq9MouseMenuKeyPosition(uint32_t* sp) {
    if (!g_key_hover) return;
    bus_write_u32(*sp + 8u, static_cast<uint32_t>(g_key_hover_x));
    bus_write_u32(*sp + 12u, static_cast<uint32_t>(g_key_hover_y));
}

// Before `cmp r7, #0` at 0x0215F3F0: r7 is the key found (0 for none). On
// the hover it becomes the cursor (*kb) and r7 is cleared, so nothing is
// typed.
NDS_TITLE_HOOK void Dq9MouseMenuKeyFound(uint32_t* key, uint32_t* kb) {
    if (!g_key_hover) return;
    g_key_hover = false;
    if (*key != 0u) bus_write_u32(*kb, *key);
    *key = 0u;
}

// The top screen (see HANDOFF_TOP_SCREEN.md and config/hooks/dq9_arm9_ov017.toml).
//
// A click on one of the party's names in the row at the bottom of the top
// screen (the overworld) opens that member's equipment page, as choosing
// Equipment and that member in the camp menu does: the camp menu calls the
// field's 0x021A120C(field, 3, slot), which sets the page scene's kind and
// member and queues it. The hook is on the entry of the field step
// (0x0218CBD4(field)) and jumps to that function with r1 = 3, r2 = slot, so
// the step is skipped for one frame and the call returns to its caller.

namespace {

constexpr uint32_t kCtx = 0x020F33D8u;          // main loop context
constexpr uint32_t kCtxMode = kCtx + 4u;        // u8
constexpr uint32_t kCtxPartyEntity = kCtx + 8u; // u32 per slot 0..3
constexpr uint32_t kCtxLeader = 14716u;         // u8, slot of the leader
constexpr uint32_t kFieldQueue = 14076u;        // scene queue, head at +0
constexpr uint32_t kMessageOwner = 0x020421BCu; // literal -> +28 = messages
constexpr uint32_t kEntityBusy = 620u;          // u8
constexpr uint32_t kEntityActing = 196u;        // u16, low 15 bits

// The row of names at the bottom of the top screen: the clicks on each
// name's rectangle were at y 176 to 188, four even slots of 64 px. Only the
// top edge is needed (the names are the last thing at the bottom).
constexpr int kRowTop = 172;

// Why the field is not idle, as a bit mask (0 = idle): for the log.
unsigned FieldBusy(uint32_t field) {
    unsigned why = 0;
    if (field == 0) return 1;
    if (bus_read_u8(kCtxMode) != 0) why |= 2;
    if (dq9::TitleScreen()) why |= 4;
    if (dq9::InBattle()) why |= 8;
    // A scene (camp menu, shop, battle...) is queued or running.
    // The field's own scene (kind 0, seen in the log; 1 is 0x021ACDF4) is at
    // the head when nothing else runs.
    const uint32_t head = bus_read_u32(field + kFieldQueue);
    if (head != 0 && bus_read_u8(head) > 1) why |= 16;
    const uint32_t leader = bus_read_u8(kCtx + kCtxLeader);
    const uint32_t player =
        leader < 4 ? bus_read_u32(kCtxPartyEntity + 4u * leader) : 0u;
    if (player == 0) {
        why |= 32;
    } else {
        if (bus_read_u8(player + kEntityBusy) != 0) why |= 64;
        if ((bus_read_u16(player + kEntityActing) & 0x7FFFu) != 0) why |= 128;
    }
    const uint32_t messages = bus_read_u32(bus_read_u32(kMessageOwner) + 28u);
    if (messages != 0 && bus_read_u32(messages + 2456u) != 0) why |= 256;
    return why;
}

}  // namespace

// Entry of 0x0218CBD4 (the field step, r0 = field): true jumps to 0x021A120C
// with r1 = 3 (the equipment page) and r2 = the member's slot.
NDS_TITLE_HOOK bool Dq9TopScreenClick(uint32_t* field, uint32_t* kind,
                                      uint32_t* slot) {
    static unsigned seen = 0;
    int x = 0, y = 0;
    const unsigned clicks = nds_title_top_click(&x, &y);
    if (clicks == seen) return false;
    seen = clicks;
    const unsigned busy = FieldBusy(*field);
    const bool idle = busy == 0;
    // The names row follows the party order, a list of character ids (not
    // always 0..3 in order): ctx+22296, count at ctx+22300.
    int members[4], count = bus_read_u8(kCtx + 22300u);
    if (count > 4) count = 4;
    for (int i = 0; i < count; ++i) members[i] = bus_read_u8(kCtx + 22296u + i);
    int hit = -1;
    if (idle && count > 0 && y >= kRowTop) hit = x * count / 256;
    std::fprintf(stderr,
                 "[dq9-top] click #%u xy=%d,%d idle=%d busy=0x%X members=%d -> %d\n",
                 clicks, x, y, idle ? 1 : 0, busy, count,
                 hit >= 0 ? members[hit] : -1);
    if (hit < 0) return false;
    *kind = 3u;
    *slot = static_cast<uint32_t>(members[hit]);
    return true;
}

// ---- Hover raise of the names row -----------------------------------------
// The party's names on the top screen are four 64x64 OBJs (attr0 = 0x00B0:
// y 176, square; attr1 size 3) in the top engine's OAM (engine B, the upper
// screen in the field). The game rebuilds the OAM shadow buffers every frame
// and copies them to OAM (0x0203BDC0; shadow B at 0x021056B4, copied by
// 0x020C59E0). The hook sits right before that copy, so the sprite under the
// pointer goes out kRaise px higher; the next frame rebuilds it.
NDS_TITLE_HOOK void Dq9NamesHoverUpload() {
    constexpr uint32_t kShadowB = 0x021056B4u;
    constexpr uint32_t kRest = 176u, kRaise = 4u;
    int px = 0, py = 0;
    if (!nds_title_top_pointer(&px, &py) || py < 172) return;
    for (uint32_t i = 0; i < 128; ++i) {
        const uint32_t at = kShadowB + 8u * i;
        const uint32_t a0 = bus_read_u16(at), a1 = bus_read_u16(at + 2);
        if ((a0 & 0xFF00u) != 0 || (a1 & 0xC000u) != 0xC000u) continue;
        if ((a0 & 0xFFu) != kRest) continue;
        const int x = static_cast<int>(a1 & 0x1FFu);
        if (px >= x && px < x + 64)
            bus_write_u16(at, static_cast<uint16_t>(kRest - kRaise));
    }
}
