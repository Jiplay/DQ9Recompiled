// Battle multipliers (Options > Multipliers, [title] mult_* in
// settings.toml, 1x by default): scale the damage the party deals and takes,
// the EXP and gold a battle gives, and the chance of item drops. Values are
// hundredths (100 = 1x).
//
// Alone or hosting, the values are this machine's settings. Visiting another
// player's world they are the host's, and the Options pages show the rows
// locked (options_model.h Locked()): in multiplayer every machine simulates
// the battle itself (lockstep), so all must use the same values. The host
// sends them to its guests with the runner's title message (wifi_net.h
// nds_netplay_title_message): "DQ9M", version 1, then the five values as
// u16 (dealt, taken, EXP, gold, drops). A guest without one (none received,
// an older build, another version) plays at 1x, never at its own values.
// In multiplayer the values are taken once per battle, at the battle's
// init (ov000 0x0215CE80), so a message arriving mid-battle cannot make two
// machines' simulations differ.
//
// Hooks (config/hooks/dq9_arm9_ov023.toml, dq9_arm9_ov024.toml):
// - EXP: ov023 0x021EE4F4, where every machine goes once the battle's EXP
//   per party slot is in ctrl+22360 (computed by the leader, received by the
//   others), before the results snapshot (exp after the battle, for the
//   level-up display) and 0x021F5150 (adds it to each member) read it.
// - Gold: ov023 0x021F51EC, the result of 0x021F44D0 (this machine's gold
//   share) before 0x021F5150 stores it (ctrl+22376, the shown amount) and
//   adds it to the party's gold.
// - Damage: ov024 0x021EC7AC, the result of 0x021E6A90 (final damage of one
//   hit) before it goes to sp+0x54, the value the action handler applies and
//   shows. Scaled only when attacker and target are on opposite sides, so a
//   heal or a hit on an ally (confusion) keeps its value.
// - Drops: ov023 0x021F454C, run by the battle leader only (the others get
//   the result with message 102's drop list). Each defeated monster's rare
//   and common item is a 1/N roll (0x02032380(N) == 0 is a hit): N is
//   divided by the multiplier at 0x021F4A00 / 0x021F4AC8. The treasure-map
//   boss items are a percentage (0x0207430C(rng, 100) < chance), scaled at
//   0x021F47E0. A sure drop (1/1, 100%) stays sure below 1x.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "battle_multipliers.h"
#include "title_hooks.h"
#include "title_settings.h"
#include "wifi_net.h"

namespace {

constexpr const char* kChoices[] = {"0.25x", "0.5x", "0.75x", "1x",
                                    "1.5x",  "2x",   "3x",    "5x"};
constexpr int kValues[] = {25, 50, 75, 100, 150, 200, 300, 500};
constexpr size_t kChoiceCount = sizeof(kChoices) / sizeof(kChoices[0]);
static_assert(kChoiceCount == sizeof(kValues) / sizeof(kValues[0]), "choices");

constexpr NdsTitleSetting Multiplier(const char* key, const char* label,
                                     const char* description) {
    return {key, label, description, 25, 500, 1,
            kChoices, kChoiceCount, kValues, 100};
}

constexpr NdsTitleSetting kDamageDealt = Multiplier(
    "mult_damage_dealt", "Damage dealt",
    "Damage the party deals to enemies.");
constexpr NdsTitleSetting kDamageTaken = Multiplier(
    "mult_damage_taken", "Damage taken",
    "Damage enemies deal to the party.");
constexpr NdsTitleSetting kExp = Multiplier(
    "mult_exp", "Experience", "Experience points won in battle.");
constexpr NdsTitleSetting kGold = Multiplier(
    "mult_gold", "Gold", "Gold won in battle.");
constexpr NdsTitleSetting kDrops = Multiplier(
    "mult_drops", "Item drops", "Chance that defeated enemies drop an item.");

const int kDamageDealtId = nds_title_setting_register(&kDamageDealt);
const int kDamageTakenId = nds_title_setting_register(&kDamageTaken);
const int kExpId = nds_title_setting_register(&kExp);
const int kGoldId = nds_title_setting_register(&kGold);
const int kDropsId = nds_title_setting_register(&kDrops);

constexpr uint32_t kExpPerSlot = 22360u;   // ctrl+: u32[4], party slots
constexpr uint32_t kMaxExp = 0xFFFFFFu;    // message 106 sends 24 bits
constexpr uint32_t kMaxGold = 0x98967Fu;   // the game's gold cap
constexpr uint32_t kMaxDamage = 9999u;
constexpr uint32_t kTargetId = 0xD0u;      // sp+: s16, target actor id
constexpr uint32_t kAttackerId = 0xD2u;    // sp+: s16, attacker actor id
constexpr int kPartySlots = 4;             // actor ids 0..3; monsters 192+

// `value` * `percent` / 100, at least 1 when `value` was, at most `cap`
// (or `value`, when that is already above it).
uint32_t Scale(uint32_t value, int percent, uint32_t cap) {
    if (percent == 100 || value == 0) return value;
    uint64_t scaled = static_cast<uint64_t>(value) *
                      static_cast<uint64_t>(percent) / 100u;
    if (scaled == 0) scaled = 1;
    const uint64_t limit = value > cap ? value : cap;
    return static_cast<uint32_t>(scaled > limit ? limit : scaled);
}

bool IsParty(int16_t id) { return id >= 0 && id < kPartySlots; }

// The game's multiplayer state (0x0202C550): the comm object (what
// 0x0202AE28 returns), whose first word is set while a session is on, and
// this machine's player index (0x0202DF68), 0 for the host.
constexpr uint32_t kComm = 0x020FEFECu;
constexpr uint32_t kPlayerIndex = 0x021015A0u;  // u16

constexpr int kRows = 5;
const NdsTitleSetting* const kSettings[kRows] = {
    &kDamageDealt, &kDamageTaken, &kExp, &kGold, &kDrops};
const int* const kIds[kRows] = {&kDamageDealtId, &kDamageTakenId, &kExpId,
                                &kGoldId, &kDropsId};

// The host's message: magic, version, then one u16 per row, kSettings order.
constexpr uint8_t kMagic[4] = {'D', 'Q', '9', 'M'};
constexpr uint8_t kVersion = 1;
constexpr size_t kMessageBytes = 5 + 2 * kRows;

int Row(int id) {
    for (int row = 0; row < kRows; ++row)
        if (*kIds[row] == id) return row;
    return 0;
}

bool MultiplayerOn() { return bus_read_u32(kComm) != 0; }

// The values the host sent, or false (all 1x) without a valid message.
bool HostValues(int out[kRows]) {
    std::vector<uint8_t> m;
    if (!nds_netplay_title_message(&m) || m.size() < kMessageBytes ||
        std::memcmp(m.data(), kMagic, 4) != 0 || m[4] != kVersion)
        return false;
    for (int row = 0; row < kRows; ++row) {
        const int v = m[5 + 2 * row] | (m[6 + 2 * row] << 8);
        out[row] = v >= 25 && v <= 500 ? v : 100;
    }
    return true;
}

// The value of `row` that applies now, before any battle snapshot.
int LiveValue(int row) {
    if (!dq9::VisitingAnotherWorld())
        return nds_title_setting_value(*kIds[row]);
    int host[kRows];
    return HostValues(host) ? host[row] : 100;
}

// Taken at each battle's init while multiplayer is on; used by the battle
// hooks for the rest of that battle.
int g_snapshot[kRows];
bool g_snapshot_valid = false;

// The multiplier `id` applies in this battle.
int Multiplier(int id) {
    const int row = Row(id);
    if (g_snapshot_valid && MultiplayerOn()) return g_snapshot[row];
    return LiveValue(row);
}

// What this machine tells its guests: its own values, while hosting.
size_t HostMessage(uint8_t* out, size_t capacity) {
    if (capacity < kMessageBytes || !MultiplayerOn() ||
        dq9::VisitingAnotherWorld())
        return 0;
    std::memcpy(out, kMagic, 4);
    out[4] = kVersion;
    for (int row = 0; row < kRows; ++row) {
        const int v = nds_title_setting_value(*kIds[row]);
        out[5 + 2 * row] = static_cast<uint8_t>(v);
        out[6 + 2 * row] = static_cast<uint8_t>(v >> 8);
    }
    return kMessageBytes;
}

const bool kSourceRegistered =
    (nds_netplay_set_title_message_source(&HostMessage), true);

}  // namespace

bool dq9::VisitingAnotherWorld() {
    const bool active = bus_read_u32(kComm) != 0;
    const uint32_t index = bus_read_u16(kPlayerIndex);
    const bool visiting = active && index != 0;
    static int logged = -1;  // the state last logged, -1 = none yet
    const int state = active ? (visiting ? 2 : 1) : 0;
    if (state != logged) {
        logged = state;
        std::fprintf(stderr, "[dq9-mult] multiplayer %s, player index %u: %s\n",
                     active ? "on" : "off", index,
                     visiting ? "visiting, host's multipliers"
                              : "own multipliers");
    }
    return visiting;
}

int dq9::HostMultiplier(const std::string& key) {
    int host[kRows];
    if (!HostValues(host)) return 100;
    for (int row = 0; row < kRows; ++row)
        if (key == std::string("title.") + kSettings[row]->key)
            return host[row];
    return 100;
}

// Before `mov r5, r0` at 0x0215CE84, the battle init (r0 = battle).
NDS_TITLE_HOOK void Dq9BattleMultiplierSnapshot(uint32_t*) {
    g_snapshot_valid = MultiplayerOn();
    if (!g_snapshot_valid) return;
    for (int row = 0; row < kRows; ++row) g_snapshot[row] = LiveValue(row);
    std::fprintf(stderr,
                 "[dq9-mult] battle start: dealt %d taken %d exp %d gold %d "
                 "drops %d\n",
                 g_snapshot[0], g_snapshot[1], g_snapshot[2], g_snapshot[3],
                 g_snapshot[4]);
}

// Before `ldr r1, [r7, #0x29C]` at 0x021EE4F4 (r7 = battle ctrl).
NDS_TITLE_HOOK void Dq9BattleExpScale(uint32_t* ctrl) {
    const int percent = Multiplier(kExpId);
    if (percent == 100) return;
    for (uint32_t slot = 0; slot < 4u; ++slot) {
        const uint32_t addr = *ctrl + kExpPerSlot + slot * 4u;
        bus_write_u32(addr, Scale(bus_read_u32(addr), percent, kMaxExp));
    }
}

// Before `str r0, [r7]` at 0x021F51EC (r0 = gold won).
NDS_TITLE_HOOK void Dq9BattleGoldScale(uint32_t* gold) {
    *gold = Scale(*gold, Multiplier(kGoldId), kMaxGold);
}

// Before `str r0, [sp, #0x54]` at 0x021EC7AC (r0 = damage of the hit).
NDS_TITLE_HOOK void Dq9BattleDamageScale(uint32_t* damage, uint32_t* sp) {
    if (static_cast<int32_t>(*damage) <= 0) return;
    const int16_t attacker = static_cast<int16_t>(bus_read_u16(*sp + kAttackerId));
    const int16_t target = static_cast<int16_t>(bus_read_u16(*sp + kTargetId));
    if (IsParty(attacker) == IsParty(target)) return;
    const int percent = Multiplier(
        IsParty(target) ? kDamageTakenId : kDamageDealtId);
    *damage = Scale(*damage, percent, kMaxDamage);
}

// Before `mov r0, r1` at 0x021F4A00 / 0x021F4AC8 (r1 = N of a 1/N drop roll).
NDS_TITLE_HOOK void Dq9BattleDropOdds(uint32_t* odds) {
    const int percent = Multiplier(kDropsId);
    const int32_t n = static_cast<int32_t>(*odds);
    if (percent == 100 || n <= 1) return;
    const int64_t scaled = static_cast<int64_t>(n) * 100 / percent;
    *odds = static_cast<uint32_t>(scaled < 1 ? 1 : scaled > 0x7FFFFFFF ? 0x7FFFFFFF : scaled);
}

// Before `ldr r0, [sp, #0x14]` at 0x021F47E0 (r8 = % chance of the item).
NDS_TITLE_HOOK void Dq9BattleDropPercent(uint32_t* chance) {
    const int percent = Multiplier(kDropsId);
    const int32_t c = static_cast<int32_t>(*chance);
    if (percent == 100 || c <= 0 || c >= 100) return;
    *chance = Scale(static_cast<uint32_t>(c), percent, 100u);
}
