// The main menu's Nintendo WFC row becomes an "Options" row, in the
// language the game shows its text in, and the WFC icon stays hidden.
// Choosing the row does nothing yet. See config/hooks/dq9_arm9_ov004.toml
// for the guest code.
#include <cstring>

#include "lang.h"
#include "title_hooks.h"

namespace {

constexpr uint32_t kCpsrZ = 1u << 30;

// Entry cell fields (0x021F89F4).
constexpr uint32_t kCellText = 0x20u;   // string pointer, 0 = use the text id
constexpr uint32_t kCellLayer = 0x34u;  // u16
constexpr uint32_t kCellTextId = 0x38u; // u16

constexpr uint32_t kMainMenuLayer = 210u;
constexpr uint32_t kWfcTextId = 60u;

// Where the row's string goes: the end of overlay slot 0 (0x021536E0 ..
// 0x021842A0), past overlay 4's image and bss (they end at 0x02171060),
// so unused while overlay 4 runs the menu. Nothing in main or the title's
// overlays points into 0x02171060 .. 0x021842A0.
constexpr uint32_t kTextBuffer = 0x02184200u;
constexpr uint32_t kTextBufferSize = 0xA0u;

}  // namespace

// After 0x021F89F4 set up a main menu entry: point the WFC row at our text.
NDS_TITLE_HOOK void Dq9OptionsRowText(uint32_t* cell) {
    if (bus_read_u16(*cell + kCellLayer) != kMainMenuLayer ||
        bus_read_u16(*cell + kCellTextId) != kWfcTextId) {
        return;
    }
    const char* text = dq9::Tr("main_menu", "options",
                               bus_read_u8(dq9::kGameTextLanguage));
    const uint32_t len = static_cast<uint32_t>(
        std::strlen(text) < kTextBufferSize ? std::strlen(text)
                                            : kTextBufferSize - 1u);
    for (uint32_t i = 0; i < len; ++i)
        bus_write_u8(kTextBuffer + i, static_cast<uint8_t>(text[i]));
    bus_write_u8(kTextBuffer + len, 0u);
    bus_write_u32(*cell + kCellText, kTextBuffer);
}

// Before `bicne`: leave the icon's hidden bit set.
NDS_TITLE_HOOK void Dq9OptionsRowHideIcon(uint32_t* cpsr) { *cpsr |= kCpsrZ; }

// Before the row's vtable +0x44 / +0x7C call: action 0 instead of 10.
NDS_TITLE_HOOK void Dq9OptionsRowAction(uint32_t* r1) { *r1 = 0u; }
