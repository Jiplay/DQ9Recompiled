// Discord Rich Presence (ndsrecomp runner/src/title_presence.h), in English
// whatever language the game runs in:
//
//     <hero's name>, Lv. <level> <vocation>
//     Exploring <place> / Fighting in <place>
//
// The player's name is also the netplay chat's name on the LAN.
//
// Read-only, from the frontend thread about once a second.
//
// The party (main loop context 0x020F33D8, the object 0x0200F398 returns):
// the member entities at ctx+8 + 4 * id (0x0200FF2C), live while bit 11
// of the entity's first halfword is set (0x0200FE00). The player of this
// machine is the id at ctx+940 (0x020100B8; 0x020730F0 takes it for "me"):
// 0 alone or hosting, a guest's own index in someone else's world, where
// id 0 is the host's hero. From the entity, as
// the party's status tabs read them (0x020200D0, 0x02022DD8):
//   +308  the stats record; its name at +0, ASCII with tags for accents
//         (DecodeName), zero-terminated (+48 / +50 max HP / MP)
//   +336  the character record: +2384 the vocation (u32), +364 a u16 level
//         per vocation (0x0202054C)
// The vocations' order is the game's, as its debug menu lists them
// (data/bin/str_debug_en.bin): 0 none, 1 Warrior ... 12 Ranger.
// The map being played is the u16 at 0x020FB3F0, the map manager's first
// field (0x02024528 reads it), the field's own map during a battle too.
// Its name comes from the game's English mapname table
// (scripts/gen-map-names.py).
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include "title_presence.h"

#include "dq9_scene.h"
#include "map_names.generated.h"

namespace {

constexpr uint32_t kCtx = 0x020F33D8u;
constexpr uint32_t kCtxEntities = kCtx + 8u;
constexpr uint32_t kCtxOwnId = kCtx + 940u;
// Multiplayer has at most four players, ids 0-3.
constexpr uint32_t kPlayers = 4u;
constexpr uint16_t kEntityLive = 0x0800u;
constexpr uint32_t kEntityStats = 308u;
constexpr uint32_t kEntityCharacter = 336u;
constexpr uint32_t kCharacterVocation = 2384u;
constexpr uint32_t kCharacterLevels = 364u;
constexpr uint32_t kVocations = 13u;
constexpr const char* kVocationNames[kVocations] = {
    nullptr,  "Warrior",   "Priest",        "Mage",    "Martial Artist",
    "Thief",  "Minstrel",  "Gladiator",     "Armamentalist", "Paladin",
    "Sage",   "Luminary",  "Ranger",
};
// Room for 8 letters, each an accent tag at worst.
constexpr uint32_t kNameBytes = 33u;
constexpr uint32_t kMapId = 0x020FB3F0u;

// Main RAM only: the pointers are read from the guest, and anything else
// (I/O above all) must not be touched.
bool InMainRam(uint32_t address, uint32_t size) {
    return address >= 0x02000000u && address + size <= 0x02400000u;
}

// The game's text is ASCII, with accented letters as tags: <:a> (umlaut),
// <'e> (acute), <`a> (grave), <^o> (circumflex), <~n> (tilde), <,c>
// (cedilla), <ss> (sharp s), as in res/lang. The renderer looks glyphs up by
// these byte sequences (0x020424F4). A tag becomes its UTF-8 letter; a tag
// or byte not known becomes '?'.
std::string TagToUtf8(const std::string& tag) {
    if (tag == "ss") return "\xC3\x9F";
    if (tag == "1") return "'";
    if (tag == ",") return ",";
    if (tag.size() != 2) return "?";
    static const char kMarks[] = ":'`^~,";
    // Latin-1 code points of each mark over a e i o u n c, upper then lower
    // case (0 = none).
    static const uint16_t kTable[6][14] = {
        {0xC4, 0xCB, 0xCF, 0xD6, 0xDC, 0, 0, 0xE4, 0xEB, 0xEF, 0xF6, 0xFC, 0, 0},
        {0xC1, 0xC9, 0xCD, 0xD3, 0xDA, 0, 0, 0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0, 0},
        {0xC0, 0xC8, 0xCC, 0xD2, 0xD9, 0, 0, 0xE0, 0xE8, 0xEC, 0xF2, 0xF9, 0, 0},
        {0xC2, 0xCA, 0xCE, 0xD4, 0xDB, 0, 0, 0xE2, 0xEA, 0xEE, 0xF4, 0xFB, 0, 0},
        {0, 0, 0, 0, 0, 0xD1, 0, 0, 0, 0, 0, 0, 0xF1, 0},
        {0, 0, 0, 0, 0, 0, 0xC7, 0, 0, 0, 0, 0, 0, 0xE7},
    };
    static const char kLetters[] = "AEIOUNCaeiounc";
    const char* mark = std::strchr(kMarks, tag[0]);
    const char* letter = std::strchr(kLetters, tag[1]);
    if (!mark || !letter || !*mark || !*letter) return "?";
    const uint16_t cp = kTable[mark - kMarks][letter - kLetters];
    if (!cp) return "?";
    return {static_cast<char>(0xC0 | (cp >> 6)),
            static_cast<char>(0x80 | (cp & 0x3F))};
}

std::string DecodeName(const uint8_t* bytes, size_t size) {
    std::string out;
    for (size_t i = 0; i < size && bytes[i]; ++i) {
        const uint8_t c = bytes[i];
        if (c == '<') {
            size_t end = i + 1;
            while (end < size && bytes[end] && bytes[end] != '>' && end - i <= 3)
                ++end;
            if (end < size && bytes[end] == '>') {
                out += TagToUtf8(std::string(
                    reinterpret_cast<const char*>(bytes + i + 1), end - i - 1));
                i = end;
                continue;
            }
        }
        out += c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '?';
    }
    return out;
}

const char* MapName(uint16_t id) {
    const auto* end = std::end(dq9::kMapNames);
    const auto* it = std::lower_bound(
        std::begin(dq9::kMapNames), end, id,
        [](const dq9::MapName& m, uint16_t key) { return m.id < key; });
    return it != end && it->id == id ? it->name : nullptr;
}

// This machine's player's name, vocation and level (the hero, or a guest's
// own character), or false outside the game.
bool Hero(std::string* name, const char** vocation_name, int* level) {
    const uint32_t id = bus_read_u32(kCtxOwnId);
    if (id >= kPlayers) return false;
    const uint32_t hero = bus_read_u32(kCtxEntities + 4u * id);
    if (!InMainRam(hero, 0x200) || !(bus_read_u16(hero) & kEntityLive))
        return false;
    const uint32_t stats = bus_read_u32(hero + kEntityStats);
    const uint32_t character = bus_read_u32(hero + kEntityCharacter);
    if (!InMainRam(stats, kNameBytes) ||
        !InMainRam(character, kCharacterVocation + 4))
        return false;
    uint8_t bytes[kNameBytes];
    for (uint32_t i = 0; i < kNameBytes; ++i) bytes[i] = bus_read_u8(stats + i);
    *name = DecodeName(bytes, kNameBytes);
    const uint32_t vocation = bus_read_u32(character + kCharacterVocation);
    if (name->empty() || vocation >= kVocations || !kVocationNames[vocation])
        return false;
    *vocation_name = kVocationNames[vocation];
    *level = bus_read_u16(character + kCharacterLevels + 2u * vocation);
    return *level > 0 && *level <= 99;
}

bool Read(NdsTitlePresence* out) {
    // The main loop's mode byte (6: boot and title), not dq9::TitleScreen(),
    // whose memory of the title's overlay belongs to the CPU thread.
    if (bus_read_u8(kCtx + 4u) == 6u) {
        out->details = "On the title screen";
        out->state.clear();
        return true;
    }
    std::string name;
    const char* vocation = nullptr;
    int level = 0;
    if (!Hero(&name, &vocation, &level)) return false;
    out->player = name;
    out->details =
        name + ", Lv. " + std::to_string(level) + " " + vocation;
    const bool battle = dq9::InBattle();
    out->state = battle ? "Fighting" : "Exploring";
    if (const char* place = MapName(bus_read_u16(kMapId)))
        out->state += std::string(battle ? " in " : " ") + place;
    return true;
}

// The frontend thread reads guest memory while the game runs, so a read can
// catch a load half done: a reading is only shown once the next one agrees.
bool Presence(NdsTitlePresence* out) {
    static NdsTitlePresence shown, last;
    static bool have_shown = false, have_last = false;
    NdsTitlePresence now;
    const bool ok = Read(&now);
    if (ok && have_last && now.details == last.details &&
        now.state == last.state) {
        shown = now;
        have_shown = true;
    }
    last = now;
    have_last = ok;
    if (!have_shown) return false;
    *out = shown;
    return true;
}

const bool kRegistered = (nds_title_set_presence_query(Presence), true);

}  // namespace
