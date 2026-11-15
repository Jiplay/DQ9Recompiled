// Screen layouts per game mode: some modes force both screens stacked at the
// same size, whatever Display > screen_layout says (the single-screen layout
// is the one that changes; the others already show both).
//
// The title screen (dq9::TitleScreen: the boot logos, the menu and the
// fade-out after Continue) uses both screens, so it is shown as on the DS,
// the top screen above the touchscreen. Add a mode to Wanted() to give it
// the same layout.
//
// In battle the second screen stays below the main one in a portrait window,
// where the overworld and the other scenes put it above.
#include <atomic>

#include "title_hooks.h"
#include "title_settings.h"

#include "dq9_scene.h"

namespace {

constexpr uint32_t kMainMode = 0x020F33D8 + 4;
constexpr uint8_t kTitleMode = 6;

// The title screen is judged at G3_SwapBuffers, where the overlay slots are
// consistent: the frontend thread reading them mid-load flickered between
// layouts during the fade-out. The mode byte is a single byte, safe to read
// at any time, so the boot and the menu follow it at once.
std::atomic<bool> g_title_at_swap{false};
std::atomic<bool> g_battle_at_swap{false};

// Called once per presented frame on the frontend thread.
bool Wanted() {
    return bus_read_u8(kMainMode) == kTitleMode ||
           g_title_at_swap.load(std::memory_order_relaxed);
}

// Called once per presented frame on the frontend thread. Judged at the swap
// like the title, since the overlay slots are inconsistent mid-load.
bool InsetBelow() { return g_battle_at_swap.load(std::memory_order_relaxed); }

const bool kRegistered = (nds_title_set_stacked_layout_query(Wanted),
                          nds_title_set_inset_below_query(InsetBelow), true);

}  // namespace

namespace dq9 {

void SampleScreenLayout() {
    g_title_at_swap.store(TitleScreen(), std::memory_order_relaxed);
    g_battle_at_swap.store(InBattle(), std::memory_order_relaxed);
}

}  // namespace dq9
