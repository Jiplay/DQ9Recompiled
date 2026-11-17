// Language: switches DQ9's text language while it runs (Game > Language,
// [title] language in settings.toml). "System" (the default) leaves the
// game untouched: it reads the firmware language, set by [system] language
// in game.toml. See config/hooks/dq9_arm9.toml for the guest code.
//
// DQ9 picks the language each time it loads a text file, so the override
// reaches menus, dialogue, item names and battles as soon as they are next
// loaded (a menu that is already open keeps its text until it is reopened).
// The one text set it loads once per session, the articles and standard
// strings, is reloaded through the game's own loader on the next frame.
#include "title_hooks.h"
#include "title_settings.h"

namespace {

// Values are the DS firmware language codes (0 here means "System").
constexpr const char* kChoices[] = {"System", "English", "French",
                                    "German", "Italian", "Spanish"};

constexpr NdsTitleSetting kLanguage = {
    "language",
    "Language",
    "The game's text language. Applies right away; a menu that is open "
    "switches when you reopen it. System uses the console language.",
    0, 5, 1,
    kChoices, sizeof(kChoices) / sizeof(kChoices[0]), nullptr,
    0,
};

const int kLanguageId = nds_title_setting_register(&kLanguage);

// Firmware user settings, as the boot code leaves them in main RAM:
// +0x64 bits 0-2 are the language (OS_GetOwnerInfo reads them).
constexpr uint32_t kFirmwareLanguage = 0x027FFCE4;

// The game's context (0x0200F398 returns it): +5 is the language the game
// last read, refreshed by 0x0200FB10 before every text file load.
constexpr uint32_t kGameLanguage = 0x020F33D8 + 5;

// Session text tables loaded by 0x020E5124 (articles, standard strings):
// their arena and the first of their two 24-byte slots (bit 31 of the
// slot's first word is set once it holds a table).
constexpr uint32_t kSessionTextArena = 0x02153680;
constexpr uint32_t kSessionTextSlot = 0x02153694;

constexpr uint32_t kMainLoopHead = 0x02000E2C;

// The language code the game would settle on: as 0x0200FB10 does, any
// code it has no text for (Japanese, Chinese, Korean) becomes English.
uint32_t GameLanguage(uint32_t code) {
    return code >= 2 && code <= 5 ? code : 1;
}

uint32_t WantedLanguage() {
    const int value = nds_title_setting_value(kLanguageId);
    if (value != 0) return static_cast<uint32_t>(value);
    return GameLanguage(bus_read_u16(kFirmwareLanguage) & 7u);
}

}  // namespace

// OS_GetOwnerInfo (0x020C99D8), before `strb r2, [r4]` at 0x020C99F8:
// r2 is the firmware language it is about to return.
NDS_TITLE_HOOK void Dq9Language(uint32_t* r2) {
    const int value = nds_title_setting_value(kLanguageId);
    if (value != 0) *r2 = static_cast<uint32_t>(value);
}

// The main loop's head (0x02000E2C, `mov r0, r7`, where r0-r3, r12, lr and
// the flags are dead). When the language the game last read differs from
// the one wanted, record the new one and, if the session text tables are
// loaded, call their loader (true: jump to 0x020E5124 with lr = this
// instruction). It returns here, and the hook then finds nothing to do.
NDS_TITLE_HOOK bool Dq9LanguageReload(uint32_t* lr) {
    // The window title shows the game's name in this language (the ROM banner
    // has one per language); host state only.
    nds_title_set_language(static_cast<int>(WantedLanguage()));
    const uint32_t current = bus_read_u8(kGameLanguage);
    if (current < 1 || current > 5) return false;  // not initialised yet
    const uint32_t wanted = WantedLanguage();
    if (current == wanted) return false;
    bus_write_u8(kGameLanguage, static_cast<uint8_t>(wanted));
    if (bus_read_u32(kSessionTextArena) == 0 ||
        (bus_read_u32(kSessionTextSlot) & 0x80000000u) == 0) {
        return false;
    }
    *lr = kMainLoopHead;
    return true;
}
