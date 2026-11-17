// dq9_scene.h — which DQ9 scene is running, from guest state, for the
// presentation hooks (UI size, camera distance).
#pragma once

#include <atomic>
#include <cstdint>

#include "title_hooks.h"

namespace dq9 {

// The overlay loader (0x020A1950) keeps the id loaded in each slot as a
// signed byte at 0x01FFD364 + 6 * *(u32*)0x01FFD348 + slot (the slot of
// overlay n is the word at 0x020E8F30 + 8n): slot 0 = overlays 0..6,
// 1 = 7..14, 2 = 15..21, 3 = 22..30, 4 = 31..32, 5 = 33..34. -1 = none.
inline int LoadedOverlay(uint32_t slot) {
    const uint32_t bank = bus_read_u32(0x01FFD348);
    return static_cast<int8_t>(bus_read_u8(0x01FFD364 + 6 * bank + slot));
}

// The boot and title screens: the main loop (ctx 0x020F33D8, mode byte at
// +4) runs them in mode 6. Continue sets mode 0 at once, but the title's
// fade-out still runs from overlay 4 (slot 0) after that, so the title is
// over only when overlay 4 is gone from its slot (see text_box_size.cpp,
// InGame, which tests the same two things).
//
// The camp menu's Attributes page loads overlay 4 in slot 0 too, from the
// field (mode 0), so overlay 4 only counts as the title's when it was
// already there in the title's mode: remembered here until it leaves its
// slot.
inline bool TitleScreen() {
    constexpr uint32_t kMainMode = 0x020F33D8 + 4;
    constexpr uint8_t kTitleMode = 6;
    constexpr int kTitleOverlay = 4;
    static std::atomic<bool> title_overlay{false};
    const bool overlay = LoadedOverlay(0) == kTitleOverlay;
    if (bus_read_u8(kMainMode) == kTitleMode) {
        title_overlay.store(overlay, std::memory_order_relaxed);
        return true;
    }
    if (!overlay) {
        title_overlay.store(false, std::memory_order_relaxed);
        return false;
    }
    return title_overlay.load(std::memory_order_relaxed);
}

// Samples the scene for the forced screen layouts (screen_layouts.cpp), at a
// consistent point on the CPU thread: G3_SwapBuffers, from Dq9WindowLayers.
void SampleScreenLayout();

// In battle: the field loads overlay 0 in slot 0 for it (see
// text_box_size.cpp, InGame).
inline bool InBattle() { return LoadedOverlay(0) == 0; }

// The battle records pages (see FullScreenPage), with the Alchenomicon
// and the accolade popup (below). The battle records (camp
// menu > Battle records, overlay 8 in slot 1; the field and the camp menu have overlay 11 there) fill engine A
// with a wallpaper on BG1 (screen base 30), draw the buttons, the cursor
// and "Back" as OBJs, and Stella and her speech box in 3D on BG0, blended
// over BG1 (BLDCNT 0x0241, EVA 15 / EVB 16). Its Defeated Monster List
// loads overlay 14 in the same slot and is laid out the same way (a 3D
// monster over BG1, engine A on the top LCD).
//
// The overlay stays loaded after the camp menu closes (the field does not
// reload slot 1), and so does BLDCNT, so the page also needs its BG1 on
// screen: engine A's BG1CNT screen base is 30 on the records page
// (0x1E02 / 0x1E03) and 31 on the monster list (0x1F00 / 0x1F01), and goes
// back to 0 (0x0004) as soon as the page closes; the camp menu uses 28 and
// 29. The blend is checked too: BLDCNT = 0x0241 (alpha, BG0 over BG1).
// Reading these registers has no side effects.
//
// The "awarded the accolade" popup, shown over the records page as it
// opens, keeps BG1 on base 30 and draws its frame on BG3 (0x1D08), dimming
// the rest with a brightness fade: BLDCNT = 0x00D7 while it is up.
//
// The Alchenomicon (records page > Alchenomicon) loads overlay 6 in slot 0,
// with the records overlay still in slot 1, and sets BG1CNT 0x1D00 (BG2
// 0x1E01, BG3 0x1F0B; BLDCNT 0x0144). The field keeps 0x0004 there after
// it closes.
inline bool RecordsPage() {
    constexpr uint32_t kFieldSlot = 0;
    constexpr uint32_t kMenuSlot = 1;
    constexpr int kRecordsOverlay = 8;
    constexpr int kMonsterListOverlay = 14;
    constexpr int kAlchenomiconOverlay = 6;
    constexpr uint32_t kBg1cntA = 0x0400000A;
    constexpr uint32_t kBldcntA = 0x04000050;
    constexpr uint16_t kPageBlend = 0x0241;
    constexpr uint16_t kAccoladeBlend = 0x00D7;
    constexpr uint16_t kAlchenomiconBg1 = 0x1D00;
    const int menu = LoadedOverlay(kMenuSlot);
    if (menu != kRecordsOverlay && menu != kMonsterListOverlay) return false;
    const uint16_t bg1 = bus_read_u16(kBg1cntA);
    if (menu == kRecordsOverlay && bg1 == kAlchenomiconBg1 &&
        LoadedOverlay(kFieldSlot) == kAlchenomiconOverlay)
        return true;
    const int screen_base = (bg1 >> 8) & 0x1F;
    if (screen_base != 30 && screen_base != 31) return false;
    const uint16_t blend = bus_read_u16(kBldcntA);
    return blend == kPageBlend || blend == kAccoladeBlend;
}

// Character pages: the Attributes page (below), the equipment screen (camp menu > Equipment, overlay 5
// in slot 0) and character creation (overlay 9 in slot 1). They fill
// engine A with 2D layers and frame the character's 3D model beside them.
// Engine A's BG1CNT is 0x1D03 on both (screen base 29, priority 3; BG2/BG3
// 0x1E02/0x1F04 on the equipment screen, 0x1E09/0x1F0C on creation). The
// field has 0x0004, and the camp menu (overlay 2 in slot 0) 0x1D02, which
// must not match: its windows are scaled. The overlays stay loaded after
// the page closes, so BG1 is what tells the page apart.
//
// The Attributes page (camp menu > Attributes) loads overlay 4 in slot 0,
// the title's overlay, with BG1CNT 0x1D00 (BG2 0x1E09/0x1E0A, BG3 0x1F0B);
// the title screen has 0x0004 / 0x0104 / 0x0105 there.
inline bool CharacterPage() {
    constexpr uint32_t kFieldSlot = 0;
    constexpr uint32_t kMenuSlot = 1;
    constexpr int kAttributesOverlay = 4;
    constexpr int kEquipmentOverlay = 5;
    constexpr int kCreationOverlay = 9;
    constexpr uint32_t kBg1cntA = 0x0400000A;
    constexpr uint16_t kPageBg1 = 0x1D03;
    constexpr uint16_t kAttributesBg1 = 0x1D00;
    const uint16_t bg1 = bus_read_u16(kBg1cntA);
    if (bg1 == kAttributesBg1)
        return LoadedOverlay(kFieldSlot) == kAttributesOverlay;
    if (bg1 != kPageBg1) return false;
    return LoadedOverlay(kFieldSlot) == kEquipmentOverlay ||
           LoadedOverlay(kMenuSlot) == kCreationOverlay;
}

// Full-screen pages, left as on the DS by the UI size and the camera
// distance: the battle records pages and the character pages.
inline bool FullScreenPage() { return RecordsPage() || CharacterPage(); }

}  // namespace dq9
