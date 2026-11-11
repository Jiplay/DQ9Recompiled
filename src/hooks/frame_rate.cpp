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

// The last main-loop pass skipped the 2D window's redraw.
bool g_text_window_skipped = false;

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
// pass (16 ms) late. While a choice list is shown the window is redrawn on
// every pass: the hand cursor (a sprite) is placed by that redraw, and
// skipping it made the cursor vanish on every other frame.
NDS_TITLE_HOOK bool Dq9TextWindowHalfRate(uint32_t* r2, uint32_t* r10,
                                          uint32_t* lr) {
    if (*lr != kMainLoopTextDraw) return false;
    const bool choice = bus_read_u32(*r10 + kWindowState) == kChoiceState &&
                        bus_read_u8(*r10 + kChoiceShown) != 0u;
    if (!Fast() || *r2 == 0u || choice || g_text_window_skipped) {
        g_text_window_skipped = false;
        return false;
    }
    g_text_window_skipped = true;
    return true;
}
