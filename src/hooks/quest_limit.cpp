// Unlimited quests (General > Unlimited quests, [title] unlimited_quests in
// settings.toml, off by default): lifts the game's limit of 8 accepted
// quests to all 204.
//
// The quest manager (0x02109418) keeps the accepted quests as 8 sorted
// 16-byte slots after its count byte (mgr+4), with other quest data right
// behind them, and the save holds exactly that block. While no more than 8
// quests are accepted everything stays there and the game runs untouched.
// Past 8, every slot lives in the free end of ITCM (kSlots), and the hooks
// point each piece of code that walks the slots there. The save keeps 8 of
// them in the game's own block; the full list goes in bytes of the save
// file the game never writes (after each of its two copies), matched to the
// block it belongs to. See config/hooks/dq9_arm9.toml for the guest code.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "io.h"
#include "save_image.h"
#include "title_hooks.h"
#include "title_settings.h"

namespace {

constexpr const char* kChoices[] = {"Off", "On"};

constexpr NdsTitleSetting kUnlimitedQuests = {
    "unlimited_quests",
    "Unlimited quests",
    "Lifts the limit of 8 quests accepted at once. Saves with more than 8 "
    "keep the rest in the save file's unused space.",
    0, 1, 1,
    kChoices, sizeof(kChoices) / sizeof(kChoices[0]), nullptr,
    0,
};

const int kUnlimitedQuestsId = nds_title_setting_register(&kUnlimitedQuests);

constexpr uint32_t kMgr = 0x02109418u;          // u8 count, then the slots
constexpr uint32_t kGameSlots = kMgr + 4u;      // the game's 8 slots
constexpr uint32_t kSeenBits = kMgr + 0x4A8u;   // cleared by remove
constexpr uint32_t kQuestStates = 0x02108844u + 716u;  // 4 bits per quest
constexpr uint32_t kGameMax = 8u;
constexpr uint32_t kQuests = 204u;
constexpr uint32_t kSharedQuests = 0xAEu;      // 174..203 sync in multiplayer
constexpr uint32_t kSlotSize = 16u;
constexpr uint32_t kBlockSize = 0xA0u;          // what the save keeps of mgr

// Free ITCM: past the game's ITCM image and bss (0x01FFF280), only the OS
// ITCM arena, which the game never allocates from.
constexpr uint32_t kSlots = 0x01FFF280u;        // kQuests slots
constexpr uint32_t kSaveBlock = kSlots + kQuests * kSlotSize;  // kBlockSize
static_assert(kSaveBlock + kBlockSize <= 0x02000000u, "past ITCM");

// The save file's bytes after each copy of the game's image: the game
// writes [0x10, 0x6FF4) and [0x8010, 0xEFF4).
constexpr uint32_t kTails[] = {0x6FF4u, 0xEFF4u};
constexpr uint32_t kTailRoom = 0x8000u - 0x6FF4u;
constexpr char kMagic[8] = {'D', 'Q', '9', 'R', 'Q', 'X', 0, 1};
constexpr uint32_t kTailHeader = 16u;  // magic, fingerprint, count, pad
static_assert(kTailHeader + kQuests * kSlotSize + 4u <= kTailRoom, "tail");

struct Slot {
    uint8_t bytes[kSlotSize];
    uint32_t Id() const { return (bytes[0] | (bytes[1] << 8)) & 0x1FFu; }
};

bool Enabled() { return nds_title_setting_value(kUnlimitedQuestsId) != 0; }
uint32_t Count() { return bus_read_u8(kMgr); }
uint32_t Limit() { return Enabled() ? kQuests : kGameMax; }
uint32_t Base(uint32_t count) { return count > kGameMax ? kSlots : kGameSlots; }

Slot ReadSlot(uint32_t addr) {
    Slot s;
    for (uint32_t i = 0; i < kSlotSize; ++i) s.bytes[i] = bus_read_u8(addr + i);
    return s;
}

void WriteSlot(uint32_t addr, const Slot& s) {
    for (uint32_t i = 0; i < kSlotSize; ++i) bus_write_u8(addr + i, s.bytes[i]);
}

std::vector<Slot> ReadSlots(uint32_t base, uint32_t count) {
    std::vector<Slot> slots(count);
    for (uint32_t i = 0; i < count; ++i)
        slots[i] = ReadSlot(base + i * kSlotSize);
    return slots;
}

// Stores `slots` (sorted) where their count says and sets the count.
void WriteSlots(const std::vector<Slot>& slots) {
    const uint32_t count = static_cast<uint32_t>(slots.size());
    const uint32_t base = Base(count);
    for (uint32_t i = 0; i < count; ++i) WriteSlot(base + i * kSlotSize, slots[i]);
    bus_write_u8(kMgr, static_cast<uint8_t>(count));
}

uint32_t QuestState(uint32_t id) {
    const uint8_t b = bus_read_u8(kQuestStates + id / 2u);
    return ((id & 1u) ? b >> 4 : b) & 3u;
}

// As 0x0206E174 does, without its wireless notification.
void SetQuestState(uint32_t id, uint32_t state) {
    const uint32_t addr = kQuestStates + id / 2u;
    const uint8_t b = bus_read_u8(addr);
    const uint8_t v = (id & 1u)
        ? static_cast<uint8_t>((b & 0x0Fu) | ((((b >> 4) & 0xCu) | state) << 4))
        : static_cast<uint8_t>((b & 0xF0u) | (b & 0xCu) | state);
    bus_write_u8(addr, v);
}

void PutU32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}

// The 8 slots the game's own block keeps: the quests whose slot the game
// reads without checking it exists first (0x0208FC40: 0xAE, 0xC1; 0x021A7378:
// 0xBC), then the lowest ids; sorted, as the game keeps them.
std::vector<Slot> KeptSlots(const std::vector<Slot>& slots) {
    std::vector<Slot> kept;
    for (const uint32_t id : {0xAEu, 0xC1u, 0xBCu})
        for (const Slot& s : slots)
            if (s.Id() == id && kept.size() < kGameMax) kept.push_back(s);
    for (const Slot& s : slots) {
        if (kept.size() >= kGameMax) break;
        const bool in = std::any_of(kept.begin(), kept.end(),
                                    [&](const Slot& k) { return k.Id() == s.Id(); });
        if (!in) kept.push_back(s);
    }
    std::sort(kept.begin(), kept.end(),
              [](const Slot& a, const Slot& b) { return a.Id() < b.Id(); });
    return kept;
}

uint32_t BlockFingerprint(uint32_t addr) {
    uint8_t block[kBlockSize];
    for (uint32_t i = 0; i < kBlockSize; ++i) block[i] = bus_read_u8(addr + i);
    return dq9::save::Crc32(block, kBlockSize);
}

void ClearTails() {
    const uint8_t* save = nullptr;
    uint32_t size = 0;
    bool dirty = false;
    if (!nds_io_cartridge_save_snapshot(&save, &size, &dirty)) return;
    for (const uint32_t tail : kTails) {
        if (tail + kTailRoom > size ||
            std::memcmp(save + tail, kMagic, sizeof(kMagic)) != 0)
            continue;
        const std::vector<uint8_t> zero(kTailRoom, 0u);
        nds_io_cartridge_save_patch(tail, zero.data(), kTailRoom);
    }
}

}  // namespace

// After an instruction that points a register at the game's slots
// (mgr+4): while the slots are in ITCM, point it there.
NDS_TITLE_HOOK void Dq9QuestSlotBase(uint32_t* base) {
    if (*base == kGameSlots && Count() > kGameMax) *base = kSlots;
}

// Before `cmp r0, #8` at 0x020961D4 (accept) with r0 = the count: let the
// accept go on while under the limit.
NDS_TITLE_HOOK void Dq9QuestAcceptLimit(uint32_t* count) {
    if (Enabled() && *count < kQuests) *count = 0u;
}

// Before `cmp r1, #0` at 0x020617F0 (script condition 0x57, "the quest log
// is full" / "is not full") with r0 = the count: full means at the limit.
NDS_TITLE_HOOK void Dq9QuestFullCheck(uint32_t* count) {
    *count = *count >= Limit() ? kGameMax : 0u;
}

// Before `cmp r0, #8` at 0x02096318 (insert; r0 = count, r5 = mgr, the new
// slot at sp+0x94). At 8 or more it inserts into the ITCM slots itself and
// jumps to the game's tail (state 2, ++count, return 1). A duplicate id
// sets r0 = 8 so the game returns 0, as it does for one.
NDS_TITLE_HOOK bool Dq9QuestInsert(uint32_t* r0, uint32_t* r5, uint32_t* sp) {
    const uint32_t count = *r0;
    if (*r5 != kMgr || count < kGameMax) return false;
    if (count >= Limit()) return false;  // the game refuses (count >= 8)
    std::vector<Slot> slots = ReadSlots(Base(count), count);
    const Slot add = ReadSlot(*sp + 0x94u);
    for (const Slot& s : slots) {
        if (s.Id() == add.Id()) {
            *r0 = kGameMax;
            return false;
        }
    }
    const auto at = std::find_if(slots.begin(), slots.end(),
                                 [&](const Slot& s) { return s.Id() > add.Id(); });
    slots.insert(at, add);
    // The count is the game's to raise; the slots go where it will say.
    for (size_t i = 0; i < slots.size(); ++i)
        WriteSlot(kSlots + static_cast<uint32_t>(i) * kSlotSize, slots[i]);
    return true;
}

// At the entry of remove 0x0209646C (r0 = mgr, r1 = quest id). With the
// slots in ITCM it does the whole removal: the +0x4A8 bit, the slot, the
// count, and the slots back to the game's array at 8. r0 = 1 if removed.
NDS_TITLE_HOOK bool Dq9QuestRemove(uint32_t* r0, uint32_t* r1) {
    const uint32_t count = Count();
    if (*r0 != kMgr || count <= kGameMax) return false;
    const int32_t id = static_cast<int32_t>(*r1);
    std::vector<Slot> slots = ReadSlots(kSlots, count);
    const auto it = std::find_if(slots.begin(), slots.end(), [&](const Slot& s) {
        return id >= 0 && s.Id() == static_cast<uint32_t>(id);
    });
    if (it == slots.end()) {
        *r0 = 0u;
        return true;
    }
    if (static_cast<uint32_t>(id) <= kQuests) {  // 0x020960DC
        const uint32_t addr = kSeenBits + (static_cast<uint32_t>(id) >> 3);
        bus_write_u8(addr, static_cast<uint8_t>(bus_read_u8(addr) &
                                                ~(1u << (id & 7))));
    }
    slots.erase(it);
    WriteSlots(slots);
    *r0 = 1u;
    return true;
}

// Before the copy of mgr[0, 0xA0) into the save image at 0x020AA928
// (r1 = mgr, r2 = 0xA0). Past 8 slots the image gets a block with the 8
// kept ones instead, and the save file's tails the full list.
NDS_TITLE_HOOK void Dq9QuestSave(uint32_t* r1, uint32_t* r2) {
    if (*r1 != kMgr || *r2 != kBlockSize) return;
    const uint32_t count = Count();
    if (count <= kGameMax) {
        ClearTails();
        return;
    }
    const std::vector<Slot> slots = ReadSlots(kSlots, count);
    const std::vector<Slot> kept = KeptSlots(slots);
    for (uint32_t i = 0; i < kBlockSize; ++i)
        bus_write_u8(kSaveBlock + i, bus_read_u8(kMgr + i));
    bus_write_u8(kSaveBlock, static_cast<uint8_t>(kept.size()));
    for (uint32_t i = 0; i < kGameMax; ++i)
        WriteSlot(kSaveBlock + 4u + i * kSlotSize,
                  i < kept.size() ? kept[i] : Slot{});
    *r1 = kSaveBlock;

    std::vector<uint8_t> tail(kMagic, kMagic + sizeof(kMagic));
    PutU32(tail, BlockFingerprint(kSaveBlock));
    PutU32(tail, count);  // u16 count, u16 0
    for (const Slot& s : slots) tail.insert(tail.end(), s.bytes, s.bytes + kSlotSize);
    PutU32(tail, dq9::save::Crc32(tail.data(), tail.size()));
    tail.resize(kTailRoom, 0u);
    for (const uint32_t at : kTails)
        nds_io_cartridge_save_patch(at, tail.data(), kTailRoom);
}

// After a loader copied the save's block into mgr (before 0x020AB104 and
// 0x020AB4C0): a tail written with this very block brings back the full
// list. A save left with 8 quests and none (it went through a runner or
// emulator without this) gets back to the giver each accepted quest that
// lost its slot, rather than keeping it accepted with no slot.
NDS_TITLE_HOOK void Dq9QuestLoad() {
    const uint32_t count = Count();
    if (count > kGameMax) return;  // not a block this game would save
    const uint32_t fingerprint = BlockFingerprint(kMgr);
    const uint8_t* save = nullptr;
    uint32_t size = 0;
    bool dirty = false;
    if (nds_io_cartridge_save_snapshot(&save, &size, &dirty)) {
        for (const uint32_t at : kTails) {
            if (at + kTailRoom > size) continue;
            const uint8_t* t = save + at;
            if (std::memcmp(t, kMagic, sizeof(kMagic)) != 0) continue;
            const uint32_t fp = dq9::save::ReadU32(t + 8);
            const uint32_t n = dq9::save::ReadU32(t + 12) & 0xFFFFu;
            if (fp != fingerprint || n <= kGameMax || n > kQuests) continue;
            const uint32_t body = kTailHeader + n * kSlotSize;
            if (dq9::save::Crc32(t, body) != dq9::save::ReadU32(t + body)) continue;
            std::vector<Slot> slots;
            for (uint32_t i = 0; i < n; ++i) {
                Slot s;
                std::memcpy(s.bytes, t + kTailHeader + i * kSlotSize, kSlotSize);
                const bool dup = std::any_of(slots.begin(), slots.end(),
                    [&](const Slot& k) { return k.Id() == s.Id(); });
                if (s.Id() < kQuests && !dup && QuestState(s.Id()) == 2u)
                    slots.push_back(s);
            }
            std::sort(slots.begin(), slots.end(),
                      [](const Slot& a, const Slot& b) { return a.Id() < b.Id(); });
            WriteSlots(slots);
            return;
        }
    }
    if (count != kGameMax) return;
    // Quests from 174 on are left alone: in multiplayer the game shares
    // their state between players (message 0x9B kind 7, 0x021D097C ->
    // 0x0206E758 -> 0x0206E568) but not their slots, so the game itself
    // can have one accepted with no slot.
    const std::vector<Slot> slots = ReadSlots(kGameSlots, count);
    for (uint32_t id = 0; id < kSharedQuests; ++id) {
        if (QuestState(id) != 2u) continue;
        const bool has = std::any_of(slots.begin(), slots.end(),
                                     [&](const Slot& s) { return s.Id() == id; });
        if (!has) SetQuestState(id, 1u);
    }
}
