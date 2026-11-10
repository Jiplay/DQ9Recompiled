// dq9_scene.h — which DQ9 scene is running, from guest state, for the
// presentation hooks (UI size, camera distance).
#pragma once

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

// Full-screen pages, left as on the DS by the UI size and the camera
// distance. The battle records (camp menu > Battle records, overlay 8 in
// slot 1; the field and the camp menu have overlay 11 there) fill engine A
// with a wallpaper on BG1 (screen base 30), draw the buttons, the cursor
// and "Back" as OBJs, and Stella and her speech box in 3D on BG0, blended
// over BG1 (BLDCNT 0x0241, EVA 15 / EVB 16). Its Defeated Monster List
// loads overlay 14 in the same slot and is laid out the same way (a 3D
// monster over BG1, engine A on the top LCD).
inline bool FullScreenPage() {
    constexpr uint32_t kMenuSlot = 1;
    constexpr int kRecordsOverlay = 8;
    constexpr int kMonsterListOverlay = 14;
    const int menu = LoadedOverlay(kMenuSlot);
    return menu == kRecordsOverlay || menu == kMonsterListOverlay;
}

}  // namespace dq9
