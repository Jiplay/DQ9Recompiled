// Back to the title from the church's "turn the power OFF" screen.
//
// Telling a priest you will not go on ends in ov3 0x0215B1DC(scene): once
// the last message box closes it fades to black, shows text 110 ("Bless
// you, my child. Please turn the power OFF.") and then stays in step 3
// (scene+0x580) forever, redrawing that text; on the DS only the power
// switch leaves it. The game keeps polling the pad there.
//
// Dq9PowerOffScreen (the function's entry) notes each frame the screen is
// in step 3; the pad update (Dq9MessageTopClick, mouse_menus.cpp) calls
// Dq9PowerOffScreenInput, which on a new button press or a new touch asks
// the frontend for a console reset: the game boots again to the title (the
// logos and the movie as Skip intros says), as after a power cycle. The
// game itself is left as it is; only the host acts.
#include <chrono>

#include "title_hooks.h"
#include "title_save.h"

namespace {

constexpr uint32_t kSceneStep = 0x580u;  // u8: 3 = the power-OFF text is up
constexpr uint32_t kPowerOffStep = 3u;

// The touch manager: +0x5F nonzero and +0x24 nonzero while touching (as
// ov3's own list code tests at 0x0215C114).
constexpr uint32_t kTouch = 0x02114E54u;

using Clock = std::chrono::steady_clock;
Clock::time_point g_seen;        // last frame the screen was up
Clock::time_point g_first_seen;  // when it came up
bool g_up = false;

// The step-3 frames before a press counts, so the press that closed the
// last message box (or a held button) cannot reset at once.
constexpr auto kArmDelay = std::chrono::milliseconds(500);

bool Touching() {
    return bus_read_u8(kTouch + 0x5Fu) != 0u && bus_read_u16(kTouch + 0x24u) != 0u;
}

}  // namespace

// At 0x0215B1DC (ov3, `push {r3-r7, lr}`): r0 = the scene.
NDS_TITLE_HOOK void Dq9PowerOffScreen(uint32_t* scene) {
    if (bus_read_u8(*scene + kSceneStep) != kPowerOffStep) return;
    const auto now = Clock::now();
    if (!g_up || now - g_seen > std::chrono::milliseconds(200)) g_first_seen = now;
    g_seen = now;
    g_up = true;
}

// From the pad update, every frame: keys = the keys held this frame.
void Dq9PowerOffScreenInput(uint32_t keys) {
    static uint32_t previous_keys = 0;
    static bool previous_touch = false;
    const bool touch = Touching();
    const uint32_t pressed = keys & ~previous_keys & 0x0FFFu;
    const bool tapped = touch && !previous_touch;
    previous_keys = keys;
    previous_touch = touch;
    if (!g_up) return;
    const auto now = Clock::now();
    if (now - g_seen > std::chrono::milliseconds(200)) {
        g_up = false;  // the screen is gone (it never is, but be safe)
        return;
    }
    if (now - g_first_seen < kArmDelay) return;
    if (pressed == 0u && !tapped) return;
    g_up = false;
    nds_title_request_reset();
}
