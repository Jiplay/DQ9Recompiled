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

}  // namespace

// Before `b 0x0218C798` at 0x0218C78C: r0 = VBlanks since the last pass,
// r7 = 2 - r0 = the loop's bound (it waits while 1 <= i < r7).
NDS_TITLE_HOOK void Dq9FrameRate(uint32_t* r0, uint32_t* r7) {
    const bool fast = nds_title_setting_value(kFrameRateId) != 0;
    nds_title_set_arm9_overclock(fast ? 2 : 1);
    if (fast) *r7 = 1u - *r0;
}
