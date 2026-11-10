// UI size: shows DQ9's windows (message boxes, menus) at a fixed size in
// window points instead of stretching them with the screen, so a big window
// shows more of the scene around them (Game > UI size, [title] ui_size in
// settings.toml). See config/hooks/dq9_arm9.toml for the guest code.
//
// The size is a multiple of the DS screen: at "2x" the UI is as big as in a
// 384-point-tall window (the default 2x window), whatever the window size.
// The scale k is (192 * n) / the main screen's height in window points, and
// never above 1: a window smaller than that shows the UI as on the DS.
// Everything scales about the screen centre (DS x 128, y 96), so the layout
// between the windows stays as on the DS and the UI stays in the middle of
// the scene. "Scale with window" turns it off.
//
// DQ9 draws its windows in two ways, handled separately below:
//   * 3D: the message manager in 3D mode draws its window with the 3D
//     engine, in a pixel-space pass (field and battle).
//   * 2D: the message manager in 2D mode and the camp menu draw into the
//     main engine's text BGs.
#include <cstdint>

#include "gpu2d.h"
#include "gpu3d.h"
#include "title_hooks.h"
#include "title_settings.h"

namespace {

constexpr const char* kChoices[] = {"Scale with window", "1x", "2x", "3x",
                                    "4x"};

constexpr NdsTitleSetting kUiSize = {
    "ui_size",
    "UI size",
    "Shows the text boxes and menus at a fixed size, as big as on a screen "
    "this many times the DS screen, so a big window shows more of the scene "
    "around them. Scale with window stretches them with the screen, as on "
    "the DS. The 3D text boxes shrink only with a 3D resolution above 1x.",
    0, 4, 1,
    kChoices, sizeof(kChoices) / sizeof(kChoices[0]), nullptr,
    2,
};

const int kUiSizeId = nds_title_setting_register(&kUiSize);

// The scale about the screen centre: 1 = unchanged.
double UiScale() {
    const int multiple = nds_title_setting_value(kUiSizeId);
    if (multiple <= 0) return 1.0;
    const int reference = 192 * multiple;
    const int points = nds_title_display_height();
    if (points <= reference) return 1.0;
    return static_cast<double>(reference) / static_cast<double>(points);
}

constexpr int kAnchorX = 128;
constexpr int kAnchorY = 96;

}  // namespace

// ---- 3D message window -----------------------------------------------------
// The field (and battle) draw sets an orthographic projection of the DS
// screen (G3_OrthoW 0..256 x 0..192, near -1024, far 1024) and then
// 0x020444F0 draws the message manager. Every piece of the window (the
// frame's border cells, the dark background, each glyph, the inline choice
// list) pushes the position matrix, translates to its pixel position at
// z = 1024 (the near plane) and pops it again, so all of it is relative to
// the position matrix current when the manager is drawn.
//
// Dq9TextBoxBegin pushes that matrix and multiplies the scale about the
// centre into it, Dq9TextBoxEnd pops it. Only the message window shrinks:
// fades, HUD sprites and emotion balloons in the same pass are drawn outside
// that call. Two limits:
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
namespace {

constexpr int32_t kOne = 0x1000;  // fx32 1.0

// GX matrix ports.
constexpr uint32_t kMtxPush = 0x04000444;
constexpr uint32_t kMtxPop = 0x04000448;
constexpr uint32_t kMtxScale = 0x0400046C;
constexpr uint32_t kMtxTrans = 0x04000470;

// The 3D window's scale, fx32 (kOne = unchanged).
int32_t TextBoxScale() {
    const int raster = nds_gpu3d_internal_scale();
    if (raster <= 1) return kOne;
    int32_t k = static_cast<int32_t>(UiScale() * kOne + 0.5);
    const int32_t sharp = (kOne + raster - 1) / raster;
    if (k < sharp) k = sharp;
    return k < kOne ? k : kOne;
}

}  // namespace

// Before each `bl 0x02066D00` in 0x020444F0 (r4 = message manager): push the
// position matrix and scale the window about the centre.
NDS_TITLE_HOOK void Dq9TextBoxBegin(uint32_t*) {
    bus_write_u32(kMtxPush, 0);
    const int32_t k = TextBoxScale();
    if (k >= kOne) return;
    // p' = a + k (p - a): translate by a (1 - k), then scale by k.
    bus_write_u32(kMtxTrans, static_cast<uint32_t>(kAnchorX * (kOne - k)));
    bus_write_u32(kMtxTrans, static_cast<uint32_t>(kAnchorY * (kOne - k)));
    bus_write_u32(kMtxTrans, 0);
    bus_write_u32(kMtxScale, static_cast<uint32_t>(k));
    bus_write_u32(kMtxScale, static_cast<uint32_t>(k));
    bus_write_u32(kMtxScale, static_cast<uint32_t>(kOne));
}

// Before each `pop {r4, pc}` that follows a Dq9TextBoxBegin.
NDS_TITLE_HOOK void Dq9TextBoxEnd() {
    bus_write_u32(kMtxPop, 1);
}

// ---- 2D windows ------------------------------------------------------------
// The other windows are text BGs on the main engine, and the presenter
// scales those itself (nds_gpu2d_set_fixed_size_layers): the BGs are shown
// at k of the game rect about the centre, their native pixels magnified n
// times instead of filling the screen. That needs no 3D resolution above 1x.
//   * The message manager's 2D mode (byte mgr+0x19CC, chosen per message by
//     the open calls 0x02044F4C / 0x0204501C): 0x020439C0 renders the box
//     into BG2 (translucent background, blended 10/6 over the 3D) and BG3
//     (text) at the fixed rect (2, 116, 252, 74), and turns BG2 and BG3 on.
//     NPC talk and the post-battle results use it.
//   * The camp menu: window backgrounds on BG1 (blended over the 3D), frames
//     and text on BG2.
// In the field and in battle BG1 is on but empty, so the mask is simply
// BG1..BG3; the BGs that are off are left alone.
//
// Set once per 3D frame, at G3_SwapBuffers; the presenter latches it when
// it composes the frame. Presentation only: nothing reaches the guest.
namespace {

constexpr uint8_t kWindowBgs = 0x0E;  // BG1..BG3

// Only in the game proper (field, battle, camp menu). The boot sequence and
// the title menu draw their art on the same BGs and are left as on the DS.
// The main loop (0x02000C9C) switches on the mode byte at ctx+4 (ctx =
// 0x020F33D8, read by 0x0200FBAC): modes 0, 4, 5 and 9 run the game
// (0x020D6CC4 with overlay 17); the boot and title screens run in mode 6.
// Overlay 17 itself is already loaded on the title, so it cannot tell them
// apart.
//
// Continue sets mode 0 at once, but the title's fade-out still runs from
// overlay 4 after that, so the game also needs overlay 4 gone from its slot.
// The overlay loader (0x020A1950) keeps the id loaded in each slot as a
// signed byte at 0x01FFD364 + 6 * *(u32*)0x01FFD348 + slot (the slot of
// overlay n is the word at 0x020E8F30 + 8n): overlays 0..6 share slot 0,
// and the field loads another one there (battle: overlay 0).
constexpr uint32_t kMainMode = 0x020F33D8 + 4;
constexpr uint32_t kOverlayBank = 0x01FFD348;
constexpr uint32_t kOverlaySlots = 0x01FFD364;
constexpr uint32_t kTitleSlot = 0;
constexpr int8_t kTitleOverlay = 4;

bool InGame() {
    switch (bus_read_u8(kMainMode)) {
        case 0: case 4: case 5: case 9: break;
        default: return false;
    }
    const uint32_t bank = bus_read_u32(kOverlayBank);
    const auto slot0 = static_cast<int8_t>(
        bus_read_u8(kOverlaySlots + 6 * bank + kTitleSlot));
    return slot0 != kTitleOverlay;
}

}  // namespace

// G3_SwapBuffers entry, once per 3D frame.
NDS_TITLE_HOOK void Dq9WindowLayers() {
    const double scale = UiScale();
    if (scale >= 1.0 || !InGame())
        nds_gpu2d_set_fixed_size_layers(0, 1.0f, 0, 0);
    else
        nds_gpu2d_set_fixed_size_layers(kWindowBgs, static_cast<float>(scale),
                                        kAnchorX, kAnchorY);
}
