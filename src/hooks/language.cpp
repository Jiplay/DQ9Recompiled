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
//
// The title screen loads its menu text and its logo once, when it starts:
// string table 402 from str_op_<LG>.bin (the main menu's rows and the save
// summary) and the top screen's background 105 from bg_up_<LG>.pac. When
// the language changes on the title, both go back to their first load step
// and the game loads them again, in the new language. The logo shows it at
// once, the main menu when it is next built (Back from the Options pages
// waits for it: TitleTextPending).
#include <cstdio>

#include "dq9_scene.h"
#include "lang.h"
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

// ---- The title's text and logo -----------------------------------------

// Widgets (ov23), in the scene's list at scene+0x118 (next at +0x18).
constexpr uint32_t kWidgetType = 0x04u;  // u16
constexpr uint32_t kWidgetId = 0x06u;    // u16
constexpr uint32_t kWidgetGroup = 0x08u;  // u16
constexpr uint32_t kWidgetNext = 0x18u;
// Every widget with a file has a load step at +0x1C: 0 asks the file
// manager for it (with "<LG>" replaced by the language then), 1 waits for
// it and parses it, 2 loaded.
constexpr uint32_t kLoadStep = 0x1Cu;
constexpr uint32_t kStepRequest = 0u, kStepLoaded = 2u;
// Both are in group 3, made by opening.stb with 24 KB.
constexpr uint32_t kTitleGroup = 3u;
// String table 402 (type 4, ov23 0x021FA298, steps 0x021FEA0C): the menu
// text, str_op_<LG>.bin in bin/menu/str_op.gp2.
constexpr uint32_t kStringTableType = 4u;
constexpr uint32_t kTextId = 402u;
// Background 105 (type 2, ov23 0x021F745C, steps 0x021FD8FC; made by ov4
// 0x0216E0C0): the logo on the top screen, bg_up_<LG>.pac in
// menu/bg_up.gp2. Its parse (0x021F7AAC) loads the CHAR and PAL chunks
// into its VRAM and appends each SCRN chunk to the screen list of its
// animation (+0x28), which counts the screens added at +0x1D and draws
// them every frame.
constexpr uint32_t kBackgroundType = 2u;
constexpr uint32_t kLogoId = 105u;
constexpr uint32_t kScreensAdded = 0x28u + 0x1Du;  // u8

// Both parses allocate in their group's heap, which frees nothing until the
// title ends: a reload there would leak with every language change. A
// reload parses into memory of its own instead: free memory at the end of
// overlay slot 0, past overlay 4's image and bss (they end at 0x02171060)
// and below options_row.cpp's strings (0x02183BE0), unused while the title
// runs. Its state is kept there too, in guest memory.
//
// The parses allocate everything through 0x02032554(heap, size): during a
// reload the group lookup gives them a node whose heap that hook answers
// from the region. The heap's own word is 0 ("no heap"), which every other
// function of the heap API returns early on. Nothing frees the region: the
// title's data lives as long as the title, which leaves it to the next
// overlay.
constexpr uint32_t kReload = 0x02171060u;
constexpr uint32_t kReloadMagic = kReload + 0x00u;
constexpr uint32_t kReloadScene = kReload + 0x04u;
constexpr uint32_t kReloadText = kReload + 0x08u;      // the string table
constexpr uint32_t kReloadLanguage = kReload + 0x0Cu;  // the language loaded
constexpr uint32_t kReloadActive = kReload + 0x10u;    // widgets reloading
constexpr uint32_t kReloadNext = kReload + 0x14u;      // next free byte
constexpr uint32_t kReloadNode = kReload + 0x18u;      // the group node
constexpr uint32_t kReloadHeap = kReloadNode + 4u;     // its heap: word 0
constexpr uint32_t kReloadLogo = kReload + 0x20u;      // the logo
constexpr uint32_t kReloadData = kReload + 0x40u;
constexpr uint32_t kReloadEnd = 0x02183BE0u;
constexpr uint32_t kMagic = 0x474C5144u;  // "DQLG"
constexpr uint32_t kActiveText = 1u, kActiveLogo = 2u;

bool InMainRam(uint32_t a) { return a >= 0x02000000u && a < 0x02400000u; }

// Widget `id` of `type` in the title's group, or 0.
uint32_t FindWidget(uint32_t scene, uint32_t id, uint32_t type) {
    uint32_t w = bus_read_u32(scene + 0x118u);
    for (int guard = 0; w && guard < 4096; ++guard) {
        if (!InMainRam(w)) return 0;
        if (bus_read_u16(w + kWidgetId) == id &&
            bus_read_u16(w + kWidgetType) == type &&
            bus_read_u16(w + kWidgetGroup) == kTitleGroup)
            return w;
        w = bus_read_u32(w + kWidgetNext);
    }
    return 0;
}

// The reload state is this scene's, with this string table.
bool ReloadIsFor(uint32_t scene, uint32_t text) {
    return bus_read_u32(kReloadMagic) == kMagic &&
           bus_read_u32(kReloadScene) == scene &&
           bus_read_u32(kReloadText) == text;
}

// A widget that reloads now (`bit` of the active ones).
bool Reloading(uint32_t widget, uint32_t address, uint32_t bit) {
    return widget && bus_read_u32(kReloadMagic) == kMagic &&
           bus_read_u32(address) == widget &&
           (bus_read_u32(kReloadActive) & bit) != 0u;
}

// Once per frame on the title, with its scene: text loaded in the language
// wanted when the table was first seen; once that differs, reload the text
// and the logo.
void TitleReload(uint32_t scene) {
    const uint32_t text = FindWidget(scene, kTextId, kStringTableType);
    if (!text) return;
    const uint32_t logo = FindWidget(scene, kLogoId, kBackgroundType);
    const uint32_t wanted = WantedLanguage();
    if (!ReloadIsFor(scene, text)) {
        bus_write_u32(kReloadMagic, kMagic);
        bus_write_u32(kReloadScene, scene);
        bus_write_u32(kReloadText, text);
        bus_write_u32(kReloadLanguage, wanted);
        bus_write_u32(kReloadActive, 0u);
        bus_write_u32(kReloadNext, kReloadData);
        bus_write_u32(kReloadHeap, 0u);
        bus_write_u32(kReloadLogo, 0u);
        return;
    }
    uint32_t active = bus_read_u32(kReloadActive);
    if (active) {
        if ((active & kActiveText) && bus_read_u32(text + kLoadStep) == kStepLoaded)
            active &= ~kActiveText;
        const uint32_t reloading_logo = bus_read_u32(kReloadLogo);
        if ((active & kActiveLogo) &&
            (reloading_logo != logo ||
             bus_read_u32(logo + kLoadStep) == kStepLoaded))
            active &= ~kActiveLogo;
        bus_write_u32(kReloadActive, active);
        if (active) return;
        std::fprintf(stderr, "[language] title text and logo reloaded (%u bytes)\n",
                     bus_read_u32(kReloadNext) - kReloadData);
    }
    if (bus_read_u32(kReloadLanguage) == wanted ||
        bus_read_u32(text + kLoadStep) != kStepLoaded)
        return;
    // Each keeps its old data until its parse, with the new file in hand,
    // replaces it; that data stays readable until then.
    active = kActiveText;
    bus_write_u32(text + kLoadStep, kStepRequest);
    if (logo && bus_read_u32(logo + kLoadStep) == kStepLoaded) {
        active |= kActiveLogo;
        bus_write_u32(logo + kLoadStep, kStepRequest);
    }
    bus_write_u32(kReloadLogo, logo);
    bus_write_u32(kReloadLanguage, wanted);
    bus_write_u32(kReloadActive, active);
    bus_write_u32(kReloadNext, kReloadData);
    bus_write_u32(kReloadHeap, 0u);
    std::fprintf(stderr, "[language] reloading the title's text%s in language %u\n",
                 logo ? " and logo" : "", wanted);
}

}  // namespace

bool dq9::TitleTextPending() {
    if (!dq9::TitleScreen() || bus_read_u32(kReloadMagic) != kMagic) return false;
    const uint32_t scene = bus_read_u32(kReloadScene);
    if (!InMainRam(scene)) return false;
    const uint32_t text = FindWidget(scene, kTextId, kStringTableType);
    if (!text || !ReloadIsFor(scene, text)) return false;
    return bus_read_u32(kReloadActive) != 0u ||
           bus_read_u32(kReloadLanguage) != WantedLanguage();
}

// ov23 0x021F68DC(widgets = scene+0x118, scene), at its entry: the scene
// step's widget update, every frame.
NDS_TITLE_HOOK void Dq9TitleWidgets(uint32_t* r0, uint32_t* r1) {
    if (!dq9::TitleScreen() || !InMainRam(*r1) || *r0 != *r1 + 0x118u) return;
    TitleReload(*r1);
}

// ov23 0x021FA670, in the string table parser 0x021FA644 (r8 = the table),
// after it looked up the table's group node (r0) for the heap at +4: a
// reload parses into its own memory.
NDS_TITLE_HOOK void Dq9TitleTextHeap(uint32_t* r0, uint32_t* r8) {
    if (*r0 && Reloading(*r8, kReloadText, kActiveText)) *r0 = kReloadNode;
}

// ov23 0x021F7AD0, in the background parser 0x021F7AAC (r10 = the
// background), after it looked up its group node (r0): a reload of the
// logo parses into its own memory, and its screens start over.
NDS_TITLE_HOOK void Dq9TitleLogoHeap(uint32_t* r0, uint32_t* r10) {
    if (!*r0 || !Reloading(*r10, kReloadLogo, kActiveLogo)) return;
    *r0 = kReloadNode;
    bus_write_u8(*r10 + kScreensAdded, 0u);
}

// The heap allocator 0x02032554(heap, size), at its entry: for the reload's
// heap, return the next bytes of its memory (true: return r0 to the caller),
// 4-aligned as the game's heaps give them, or 0 when it is full.
NDS_TITLE_HOOK bool Dq9TitleTextAlloc(uint32_t* r0, uint32_t* r1) {
    if (*r0 != kReloadHeap || bus_read_u32(kReloadMagic) != kMagic) return false;
    const uint32_t next = bus_read_u32(kReloadNext);
    const uint32_t size = (*r1 + 3u) & ~3u;
    if (next < kReloadData || next > kReloadEnd || size > kReloadEnd - next) {
        std::fprintf(stderr, "[language] title reload out of memory\n");
        *r0 = 0u;
        return true;
    }
    for (uint32_t a = next; a < next + size; a += 4) bus_write_u32(a, 0u);
    bus_write_u32(kReloadNext, next + size);
    *r0 = next;
    return true;
}

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
