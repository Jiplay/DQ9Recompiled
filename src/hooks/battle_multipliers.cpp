// Battle multipliers (Options > Multipliers, [title] mult_* in
// settings.toml, 1x by default): scale the damage the party deals and takes,
// and the EXP and gold a battle gives. Values are hundredths (100 = 1x).
//
// Solo only for now: the values are the local settings. In multiplayer every
// machine simulates the battle itself (lockstep), so the host's values will
// have to reach the guests first (HANDOFF_BATTLE_MULTIPLIERS.md).
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
#include <cstdint>

#include "title_hooks.h"
#include "title_settings.h"

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

const int kDamageDealtId = nds_title_setting_register(&kDamageDealt);
const int kDamageTakenId = nds_title_setting_register(&kDamageTaken);
const int kExpId = nds_title_setting_register(&kExp);
const int kGoldId = nds_title_setting_register(&kGold);

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

}  // namespace

// Before `ldr r1, [r7, #0x29C]` at 0x021EE4F4 (r7 = battle ctrl).
NDS_TITLE_HOOK void Dq9BattleExpScale(uint32_t* ctrl) {
    const int percent = nds_title_setting_value(kExpId);
    if (percent == 100) return;
    for (uint32_t slot = 0; slot < 4u; ++slot) {
        const uint32_t addr = *ctrl + kExpPerSlot + slot * 4u;
        bus_write_u32(addr, Scale(bus_read_u32(addr), percent, kMaxExp));
    }
}

// Before `str r0, [r7]` at 0x021F51EC (r0 = gold won).
NDS_TITLE_HOOK void Dq9BattleGoldScale(uint32_t* gold) {
    *gold = Scale(*gold, nds_title_setting_value(kGoldId), kMaxGold);
}

// Before `str r0, [sp, #0x54]` at 0x021EC7AC (r0 = damage of the hit).
NDS_TITLE_HOOK void Dq9BattleDamageScale(uint32_t* damage, uint32_t* sp) {
    if (static_cast<int32_t>(*damage) <= 0) return;
    const int16_t attacker = static_cast<int16_t>(bus_read_u16(*sp + kAttackerId));
    const int16_t target = static_cast<int16_t>(bus_read_u16(*sp + kTargetId));
    if (IsParty(attacker) == IsParty(target)) return;
    const int percent = nds_title_setting_value(
        IsParty(target) ? kDamageTakenId : kDamageDealtId);
    *damage = Scale(*damage, percent, kMaxDamage);
}
