// 3D frame rate (Game > 3D frame rate, [title] frame_rate in settings.toml,
// 30 FPS by default): lets the game's main loop draw a 3D image on every
// VBlank instead of every second one.
//
// The loop (ov17 0x0218B710, the field, towns and battles) ends each pass
// with
//
//     for (i = 1; i < 2 - ctx->vblanks; ++i) OS_WaitVBlankIntr();
//     G3_SwapBuffers(); OS_WaitVBlankIntr();
//
// where ctx (0x020F33D8) + 0x3C8 counts the VBlanks since the last pass (the
// VBlank handler at 0x02012BE8 adds one). A pass that took less than a
// VBlank so waits for a second one: 30 images a second, 20 when a pass took
// more than two. The game then times its own logic from what the pass took
// (0x02010160: milliseconds at ctx+0x3B4/+0x3B8, VBlanks at ctx+0x3C4, about
// 250 readers), so it already runs at the right speed at 30 and at 20.
//
// The hook replaces the 2 with a 1: no extra wait, so a pass that fits in
// one VBlank is shown at the next one and the game sees a 16 ms pass. A pass
// that takes longer is paced as before. See config/hooks/dq9_arm9_ov017.toml.
//
// On the DS's ARM9 a pass takes about one VBlank, so the rate would flip
// between 60 and 30. 60 FPS therefore also runs the emulated ARM9 at twice
// its hardware clock (nds_title_set_arm9_overclock), which holds 60 in towns
// too; timers, sound and the ARM7 keep the hardware's timing. 30 FPS puts
// the hardware clock back.
//
// While a dialogue box is open the loop also reads the cartridge on every
// pass (the polling read at 0x020D0BF8: about three quarters of the ARM9's
// time went there, and the rate fell to 30). The card is not on the ARM9's
// clock, so 60 FPS makes its transfers 16 times as fast as well
// (nds_title_set_card_speedup).
//
// Twice the passes is about twice the host work: a town took 16 ms of the
// 16.7 ms frame on an i7-13650HX and a dialogue box 17.7, so the host fell
// to 52 fps. 60 FPS therefore also lets the scheduler run the ARM9 in longer
// stretches while the ARM7 sleeps (nds_title_set_event_windows), which cuts
// the cost of each frame.
//
// While the text of a 2D message window is appearing, the game redoes the
// whole window on every pass, which at 60 FPS still cost about 2 ms a frame
// on that host. Dq9TextWindowHalfRate skips that redraw on every other pass
// of the main loop, so the window is redrawn 30 times a second as in the
// original while 3D stays at 60 (except while a choice list is shown).
#include "title_hooks.h"
#include "title_settings.h"

namespace {

constexpr const char* kChoices[] = {"30 FPS", "60 FPS"};

constexpr NdsTitleSetting kFrameRate = {
    "frame_rate",
    "3D frame rate",
    "60 FPS lets the game draw a 3D image on every screen refresh when it "
    "is ready in time. 30 FPS is the original.",
    0, 1, 1,
    kChoices, sizeof(kChoices) / sizeof(kChoices[0]), nullptr,
    0,
};

const int kFrameRateId = nds_title_setting_register(&kFrameRate);

bool Fast() { return nds_title_setting_value(kFrameRateId) != 0; }

// The return address of the main loop's call of 0x020439C0 (ov17
// 0x0218C910): the other callers run their own loops.
constexpr uint32_t kMainLoopTextDraw = 0x0218C914u;

// The window manager's choice list: 0x02044540 (called from the redraw,
// 0x02066D00) draws it and its hand cursor when the state at +0x9A0 is 6 and
// the byte at +0x19BA is set.
constexpr uint32_t kWindowState = 0x9A0u;
constexpr uint32_t kChoiceState = 6u;
constexpr uint32_t kChoiceShown = 0x19BAu;

// The last main-loop pass skipped the 2D window's redraw (or, with a choice
// list up, its tile upload).
bool g_text_window_skipped = false;
// This call of 0x020439C0 runs the block but skips the tile upload.
bool g_skip_tile_upload = false;

}  // namespace

// Before `b 0x0218C798` at 0x0218C78C: r0 = VBlanks since the last pass,
// r7 = 2 - r0 = the loop's bound (it waits while 1 <= i < r7).
NDS_TITLE_HOOK void Dq9FrameRate(uint32_t* r0, uint32_t* r7) {
    const bool fast = Fast();
    nds_title_set_arm9_overclock(fast ? 2 : 1);
    nds_title_set_card_speedup(fast ? 16 : 1);
    nds_title_set_event_windows(fast);
    if (fast) *r7 = 1u - *r0;
}

// Before `beq 0x02043E94` at 0x020439DC: r2 != 0 while the 2D message window
// is up, r10 = the window manager, lr = the caller. True takes the branch past
// the window's redraw. Never two passes in a row, so the window is at most one
// pass (16 ms) late. While a choice list is shown the block runs on every
// pass, since it places the hand cursor (a sprite) and skipping it made the
// cursor vanish on every other frame; on those passes only the tile upload is
// skipped (Dq9TextWindowSkipUpload).
NDS_TITLE_HOOK bool Dq9TextWindowHalfRate(uint32_t* r2, uint32_t* r10,
                                          uint32_t* lr) {
    g_skip_tile_upload = false;
    if (*lr != kMainLoopTextDraw) return false;
    if (!Fast() || *r2 == 0u || g_text_window_skipped) {
        g_text_window_skipped = false;
        return false;
    }
    g_text_window_skipped = true;
    const bool choice = bus_read_u32(*r10 + kWindowState) == kChoiceState &&
                        bus_read_u8(*r10 + kChoiceShown) != 0u;
    if (choice) {
        g_skip_tile_upload = true;
        return false;
    }
    return true;
}

// Before `beq 0x02043C98` at 0x02043C2C, later in the same call: r3 = the
// tile buffer's descriptor (0 = none). True takes the branch past the upload;
// VRAM keeps the last pass's tiles.
NDS_TITLE_HOOK bool Dq9TextWindowSkipUpload(uint32_t* r3) {
    const bool skip = g_skip_tile_upload && *r3 != 0u;
    g_skip_tile_upload = false;
    return skip;
}

namespace {

// Literals of the ITCM lookup at 0x01FF88C4: the heap's base and its block
// table (128 entries of 6 bytes: u16 offset, u16, u16 flags, bit 0 = live).
constexpr uint32_t kHeapBaseLiteral = 0x01FF8914u;
constexpr uint32_t kHeapTableLiteral = 0x01FF8918u;
constexpr uint32_t kHeapSpan = 0x5000u;
constexpr uint32_t kHeapEntries = 0x80u;
constexpr uint32_t kHeapEntrySize = 6u;

constexpr uint32_t kCpsrN = 1u << 31;
constexpr uint32_t kCpsrZ = 1u << 30;
constexpr uint32_t kCpsrC = 1u << 29;
constexpr uint32_t kCpsrV = 1u << 28;

// NZCV of `cmp a, b`.
uint32_t CmpFlags(uint32_t a, uint32_t b) {
    const uint32_t d = a - b;
    uint32_t f = 0u;
    if (d & 0x80000000u) f |= kCpsrN;
    if (d == 0u) f |= kCpsrZ;
    if (a >= b) f |= kCpsrC;
    if (((a ^ b) & (a ^ d)) & 0x80000000u) f |= kCpsrV;
    return f;
}

}  // namespace

// At the entry of the ITCM lookup 0x01FF88C4 (r1 = a pointer into the heap):
// returns r0 = the live block entry whose offset is r1 - base, or 0. At
// 60 FPS it runs the search natively and returns true (the bank then does
// `bx lr`), leaving r0-r3 and NZCV as the guest code would.
NDS_TITLE_HOOK bool Dq9HeapBlockLookup(uint32_t* r0, uint32_t* r1, uint32_t* r2,
                                       uint32_t* r3, uint32_t* cpsr) {
    if (!Fast()) return false;
    const uint32_t nzcv_mask = kCpsrN | kCpsrZ | kCpsrC | kCpsrV;
    const uint32_t off = *r1 - bus_read_u32(kHeapBaseLiteral);
    *r2 = off;
    if (off >= kHeapSpan) {
        *r0 = 0u;
        *cpsr = (*cpsr & ~nzcv_mask) | CmpFlags(off, kHeapSpan);
        return true;
    }
    uint32_t entry = bus_read_u32(kHeapTableLiteral);
    for (uint32_t i = 0u; i < kHeapEntries; ++i, entry += kHeapEntrySize) {
        *r1 = bus_read_u16(entry + 4u);
        if ((*r1 & 1u) == 0u) continue;
        *r1 = bus_read_u16(entry);
        if (*r1 == off) {
            *r0 = entry;
            *r3 = i;
            *cpsr = (*cpsr & ~nzcv_mask) | CmpFlags(*r1, off);
            return true;
        }
    }
    *r0 = 0u;
    *r3 = kHeapEntries;
    *cpsr = (*cpsr & ~nzcv_mask) | CmpFlags(kHeapEntries, kHeapEntries);
    return true;
}

// At the entry of the kerning search 0x020425F4(prev = r0, cur = r1,
// font = r2): r0 = the signed adjustment of the pair (prev & 0xFF) |
// (cur << 8) in the font's table, or 0. At 60 FPS it scans natively and
// returns true (the bank then does `bx lr`), leaving r0-r3, r12 and NZCV as
// the guest code would.
NDS_TITLE_HOOK bool Dq9KerningLookup(uint32_t* r0, uint32_t* r1, uint32_t* r2,
                                     uint32_t* r3, uint32_t* r12,
                                     uint32_t* cpsr) {
    constexpr uint32_t kFontTable = 0x0210782Cu;
    if (!Fast()) return false;
    const uint32_t nzcv_mask = kCpsrN | kCpsrZ | kCpsrC | kCpsrV;
    const uint32_t key = ((*r0 & 0xFFu) | ((*r1 << 24) >> 16)) & 0xFFFFu;
    const uint32_t font = bus_read_u32(kFontTable + (*r2 << 2));
    const uint32_t count = bus_read_u32(font + 8u);
    uint32_t entry = bus_read_u32(font + 0xCu);
    for (uint32_t i = 0u; i < count; ++i, entry += 4u) {
        *r1 = bus_read_u16(entry);
        if (*r1 == key) {
            *r0 = static_cast<uint32_t>(
                static_cast<int32_t>(static_cast<int8_t>(bus_read_u8(entry + 2u))));
            *r2 = entry;
            *r3 = count;
            *r12 = i;
            *cpsr = (*cpsr & ~nzcv_mask) | CmpFlags(*r1, key);
            return true;
        }
    }
    *r0 = 0u;
    *r2 = entry;
    *r3 = count;
    *r12 = count;
    *cpsr = (*cpsr & ~nzcv_mask) | CmpFlags(count, count);
    return true;
}
