// Unlock camera rotation: lets L and R turn DQ9's field camera all the way
// round instead of stopping 45 degrees either side of the default (Game >
// Unlock camera rotation, [title] unlock_camera_rotation in settings.toml).
// Off leaves the game untouched; L+R still resets the camera. See
// config/hooks/dq9_arm9.toml for the guest code.
//
// Camera buttons (Game > Camera buttons, [title] camera_buttons, off by
// default): two on-screen buttons (ndsrecomp runner/src/touch_buttons.h)
// turn the camera without a pad. They hold L and R, halfway down the left
// and right edges of the top screen, shown while the field is being walked.
#include <atomic>
#include <chrono>
#include <cstdint>

#include "title_hooks.h"
#include "title_settings.h"
#include "touch_buttons.h"

#include "dq9_scene.h"

namespace {

constexpr const char* kChoices[] = {"Off", "On"};

constexpr NdsTitleSetting kUnlockRotation = {
    "unlock_camera_rotation",
    "Unlock camera rotation",
    "Lets L and R turn the field camera all the way round. Off, it stops "
    "45 degrees either side, as in the original. L+R still resets it.",
    0, 1, 1,
    kChoices, sizeof(kChoices) / sizeof(kChoices[0]), nullptr,
    0,
};

const int kUnlockRotationId = nds_title_setting_register(&kUnlockRotation);

constexpr NdsTitleSetting kCameraButtons = {
    "camera_buttons",
    "Camera buttons",
    "Shows on-screen arrows that turn the field camera, for play without "
    "a pad.",
    0, 1, 1,
    kChoices, sizeof(kChoices) / sizeof(kChoices[0]), nullptr,
    0,
};

const int kCameraButtonsId = nds_title_setting_register(&kCameraButtons);

// ---- camera buttons ---------------------------------------------------------

// The field walk: in game (the main loop's modes as text_box_size.cpp's
// InGame), not on the title, not in battle, no full-screen page, and no 2D
// window up on engine A (every one turns BG2 on: the message box, the camp
// menu; the walk shows BG1 alone). Read at G3_SwapBuffers, where the
// overlay slots are consistent.
constexpr uint32_t kMainMode = 0x020F33D8 + 4;
constexpr uint32_t kDispcntA = 0x04000000;
constexpr uint32_t kBg2Bg3 = 0x0C00;

bool FieldWalk() {
    switch (bus_read_u8(kMainMode)) {
        case 0: case 4: case 5: case 9: break;
        default: return false;
    }
    return !dq9::TitleScreen() && !dq9::InBattle() &&
           !dq9::FullScreenPage() && (bus_read_u32(kDispcntA) & kBg2Bg3) == 0;
}

std::atomic<bool> g_field_at_swap{false};
std::atomic<uint32_t> g_swaps{0};

// On the frontend thread, once per frame. The sample only holds while 3D
// frames keep coming: a scene without them (or a paused game) hides the
// buttons. A scene change stops and restarts them in bursts, so the
// buttons hide at once but show again only once the field has held for a
// while, or they would flicker through it.
bool CameraButtonsShown() {
    using Clock = std::chrono::steady_clock;
    constexpr auto kStale = std::chrono::milliseconds(250);
    constexpr auto kSettle = std::chrono::milliseconds(500);
    static uint32_t seen = 0;
    static Clock::time_point last_swap{};
    static Clock::time_point held_since{};
    static bool holding = false;
    if (!nds_title_setting_value(kCameraButtonsId)) {
        holding = false;
        return false;
    }
    const Clock::time_point now = Clock::now();
    const uint32_t swaps = g_swaps.load(std::memory_order_relaxed);
    if (swaps != seen) {
        seen = swaps;
        last_swap = now;
    }
    const bool field = now - last_swap < kStale &&
                       g_field_at_swap.load(std::memory_order_relaxed);
    if (!field) {
        holding = false;
        return false;
    }
    if (!holding) {
        holding = true;
        held_since = now;
    }
    return now - held_since >= kSettle;
}

constexpr uint16_t kKeyR = 1u << 8;
constexpr uint16_t kKeyL = 1u << 9;

const NdsTouchButtonSpec kTurnLeft = {
    {
        "...........",
        "....#......",
        "...##......",
        "..#######..",
        ".########..",
        "..#######..",
        "...##......",
        "....#......",
        "...........",
    },
    NdsTouchButtonPlace::LeftMiddle,
    kKeyL,
    CameraButtonsShown,
};

const NdsTouchButtonSpec kTurnRight = {
    {
        "...........",
        "......#....",
        "......##...",
        "..#######..",
        "..########.",
        "..#######..",
        "......##...",
        "......#....",
        "...........",
    },
    NdsTouchButtonPlace::RightMiddle,
    kKeyR,
    CameraButtonsShown,
};

const bool kButtonsRegistered = (nds_touch_button_register(&kTurnLeft),
                                 nds_touch_button_register(&kTurnRight), true);

}  // namespace

namespace dq9 {

void SampleCameraButtons() {
    g_field_at_swap.store(FieldWalk(), std::memory_order_relaxed);
    g_swaps.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace dq9

// Before `cmp r6, r1` at 0x020A2364 (L) and 0x020A23C8 (R): true jumps
// past the yaw clamp to the call that sets the yaw.
NDS_TITLE_HOOK bool Dq9UnlockCameraRotation() {
    return nds_title_setting_value(kUnlockRotationId) != 0;
}
