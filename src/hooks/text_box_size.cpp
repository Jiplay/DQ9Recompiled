// Text box size: keeps DQ9's message window the size it has in the default
// 2x window instead of filling the lower half of the screen however big the
// window is (Game > Text box size, [title] text_box_size in settings.toml).
// See config/hooks/dq9_arm9.toml for the guest code.
//
// The message window is drawn with the 3D engine, in a pixel-space pass:
// the field (and battle) draw sets an orthographic projection of the DS
// screen (G3_OrthoW 0..256 x 0..192, near -1024, far 1024) and then
// 0x020444F0 draws the message manager. Every piece of the window (the
// frame's border cells, the dark background, each glyph, the inline choice
// list) pushes the position matrix, translates to its pixel position at
// z = 1024 (the near plane) and pops it again, so all of it is relative to
// the position matrix current when the manager is drawn.
//
// Dq9TextBoxBegin pushes that matrix and multiplies a scale about an anchor
// into it, Dq9TextBoxEnd pops it. The anchor is the screen's bottom centre
// (DS x 128, y 192), so the box keeps its bottom margin; a window placed in
// the top half anchors at the top centre instead. Only the message window
// shrinks: fades, HUD sprites and emotion balloons in the same pass are drawn
// outside that call.
//
// The scale k is 384 / the main screen's height in window points, so the box
// keeps its 2x size, like Auto camera distance (camera_zoom.cpp) keeps the
// characters'. Two limits:
//   * Only while the 3D renders above 1x. The game's own geometry changes, so
//     the native image shrinks too, and at 1x (or with HD off) a 4K window
//     would show a box of a few dozen DS pixels.
//   * Never below 1 / the 3D resolution scale, so a glyph's 16 texels still
//     cover at least 16 raster pixels and the text stays sharp. With a low 3D
//     resolution in a big window the box stops shrinking there.
// The CPU composite (shown while the runtime menu is open, for example)
// draws the same geometry at 1x, so there the shrunk box is blurry.
//
// The push and pop are sent whatever the setting, so they always pair up
// even when the setting or the window changes between the two hooks; the
// pair alone leaves the image unchanged.
//
// These hooks write GX matrix commands (0x04000444 and on). That is what the
// game itself does around each piece of the window, through the same FIFO;
// the hooks add at most four commands a frame.
#include <cstdint>

#include "gpu3d.h"
#include "title_hooks.h"
#include "title_settings.h"

namespace {

constexpr const char* kChoices[] = {"Fixed", "Scale with window"};

constexpr NdsTitleSetting kTextBoxSize = {
    "text_box_size",
    "Text box size",
    "Fixed keeps the text boxes the size they have in the 2x window, so a "
    "big window shows more of the scene around them (needs a 3D resolution "
    "above 1x). Scale with window stretches them with the screen, filling "
    "its lower half as on the DS.",
    0, 1, 1,
    kChoices, sizeof(kChoices) / sizeof(kChoices[0]), nullptr,
    0,
};

const int kTextBoxSizeId = nds_title_setting_register(&kTextBoxSize);

// The main screen height (window points) at which the box keeps its own
// size: the default 2x window.
constexpr int kReferencePoints = 192 * 2;

constexpr int32_t kOne = 0x1000;  // fx32 1.0

// GX matrix ports.
constexpr uint32_t kMtxPush = 0x04000444;
constexpr uint32_t kMtxPop = 0x04000448;
constexpr uint32_t kMtxScale = 0x0400046C;
constexpr uint32_t kMtxTrans = 0x04000470;

// The message manager (0x020421B0): its window frame object is at +0x19E0,
// and the frame's rectangle (s16 x, y, width, height) is pointed to by
// frame+0x440.
constexpr uint32_t kWindowRect = 0x19E0 + 0x440;

bool InMainRam(uint32_t addr) {
    return addr >= 0x02000000u && addr < 0x02400000u;
}

// The scale, fx32 (kOne = unchanged).
int32_t TextBoxScale() {
    if (nds_title_setting_value(kTextBoxSizeId) != 0) return kOne;
    const int points = nds_title_display_height();
    const int raster = nds_gpu3d_internal_scale();
    if (points <= kReferencePoints || raster <= 1) return kOne;
    int32_t k = static_cast<int32_t>(
        (static_cast<int64_t>(kReferencePoints) * kOne + points / 2) / points);
    const int32_t sharp = (kOne + raster - 1) / raster;
    if (k < sharp) k = sharp;
    return k < kOne ? k : kOne;
}

// DS y of the anchor: the bottom edge, or the top edge for a window whose
// centre is in the top half of the screen.
int32_t AnchorY(uint32_t manager) {
    const uint32_t rect = bus_read_u32(manager + kWindowRect);
    if (!InMainRam(rect)) return 192;
    const int y = static_cast<int16_t>(bus_read_u16(rect + 2));
    const int height = static_cast<int16_t>(bus_read_u16(rect + 6));
    return y + height / 2 < 96 ? 0 : 192;
}

}  // namespace

// Before each `bl 0x02066D00` in 0x020444F0 (r4 = message manager): push the
// position matrix and scale the window about its anchor.
NDS_TITLE_HOOK void Dq9TextBoxBegin(uint32_t* r4) {
    bus_write_u32(kMtxPush, 0);
    const int32_t k = TextBoxScale();
    if (k >= kOne) return;
    // p' = a + k (p - a): translate by a (1 - k), then scale by k.
    const int32_t ax = 128, ay = AnchorY(*r4);
    bus_write_u32(kMtxTrans, static_cast<uint32_t>(ax * (kOne - k)));
    bus_write_u32(kMtxTrans, static_cast<uint32_t>(ay * (kOne - k)));
    bus_write_u32(kMtxTrans, 0);
    bus_write_u32(kMtxScale, static_cast<uint32_t>(k));
    bus_write_u32(kMtxScale, static_cast<uint32_t>(k));
    bus_write_u32(kMtxScale, static_cast<uint32_t>(kOne));
}

// Before each `pop {r4, pc}` that follows a Dq9TextBoxBegin.
NDS_TITLE_HOOK void Dq9TextBoxEnd() {
    bus_write_u32(kMtxPop, 1);
}
