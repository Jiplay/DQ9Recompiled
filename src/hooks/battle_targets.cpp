// Battle targets on the top screen: while a command waits for its target
// (Attack, a spell...), a click on a monster in the 3D scene chooses that
// monster, as picking it with the keypad and pressing A would. With
// Mouse-friendly menus on, the mouse passing over a monster moves the cursor
// to it first. The touchscreen's group list is left as it is (a tap there
// still picks a monster of the group at random).
//
// The target step is ov000 0x0217B380, the command's state 14 (see
// config/hooks/dq9_arm9_ov000.toml). Each frame it takes the list widget's
// entry (a target slot) into cmd+0x1D and chooses it on a new A. The slots
// (up to 8) are in the table at *(0x0218429C): +7452 the group of each slot,
// +7484 its actor id (monsters 192..199), *(0x0218428C) of them.
//
// A monster is hit when the click is near the upright span from its feet to
// the top of its model on screen, which works like a ray cast against a
// capsule around it. The span comes from the same projection the game uses
// to put its cursor over a monster (ov026 0x021DDDCC, through NNS's
// world-to-screen 0x020B6D20): the actor's position (+0x44, fx32) and model
// height (+0x68), the camera matrix (0x0210A05C, 4x3) and the projection
// (0x0210A018, 4x4) of the NNS G3D globals, and their viewport (0x0210A0B0).
#include <chrono>
#include <cmath>
#include <cstdint>

#include "title_hooks.h"
#include "title_settings.h"

bool Dq9MouseFriendlyMenus();

namespace {

constexpr uint32_t kActorTable = 0x020F33D8u;  // + 8 + 4 * id: actor
constexpr int kActorCount = 233;
constexpr uint16_t kActorLive = 0x400u;
constexpr uint32_t kActorPos = 0x44u;     // fx32 x, y, z
constexpr uint32_t kActorHeight = 0x68u;  // fx32
constexpr uint32_t kSlotTablePtr = 0x0218429Cu;
constexpr uint32_t kSlotCount = 0x0218428Cu;
constexpr uint32_t kSlotActor = 7484u;
constexpr int kMaxSlots = 8;
constexpr int kFirstMonster = 4;  // actor ids 0..3 are the party
constexpr uint32_t kProjection = 0x0210A018u;
constexpr uint32_t kCamera = 0x0210A05Cu;
constexpr uint32_t kViewport = 0x0210A0B0u;

// Smallest reach around the span, in DS pixels, and the reach as a share of
// the span's length (a model is about as wide as it is tall).
constexpr double kMinReach = 12.0;
constexpr double kReachShare = 0.45;

struct Projector {
    int32_t cam[12];
    int32_t proj[16];
    int x1, y1, x2, y2;

    void Load() {
        for (int i = 0; i < 12; ++i)
            cam[i] = static_cast<int32_t>(bus_read_u32(kCamera + 4u * i));
        for (int i = 0; i < 16; ++i)
            proj[i] = static_cast<int32_t>(bus_read_u32(kProjection + 4u * i));
        const uint32_t vp = bus_read_u32(kViewport);
        x1 = static_cast<int>(vp & 0xFFu);
        y1 = static_cast<int>((vp >> 8) & 0xFFu);
        x2 = static_cast<int>((vp >> 16) & 0xFFu);
        y2 = static_cast<int>(vp >> 24);
    }

    // World position (fx32 units) to top-screen pixels; false behind the
    // camera.
    bool Project(double x, double y, double z, double* sx, double* sy) const {
        constexpr double k = 1.0 / 4096.0;
        const double vx = (x * cam[0] + y * cam[3] + z * cam[6]) * k + cam[9];
        const double vy = (x * cam[1] + y * cam[4] + z * cam[7]) * k + cam[10];
        const double vz = (x * cam[2] + y * cam[5] + z * cam[8]) * k + cam[11];
        const double cx =
            (vx * proj[0] + vy * proj[4] + vz * proj[8]) * k + proj[12];
        const double cy =
            (vx * proj[1] + vy * proj[5] + vz * proj[9]) * k + proj[13];
        const double cw =
            (vx * proj[3] + vy * proj[7] + vz * proj[11]) * k + proj[15];
        if (cw <= 0.0) return false;
        *sx = x1 + (cx / cw + 1.0) * 0.5 * (x2 - x1);
        *sy = 191 - y1 - (cy / cw + 1.0) * 0.5 * (y2 - y1);
        return true;
    }
};

double SegmentDistance(double px, double py, double ax, double ay, double bx,
                       double by) {
    const double dx = bx - ax, dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    double t = len2 > 0.0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

// The target slot of the monster at (x, y) on the top screen, or -1.
int MonsterSlotAt(int x, int y) {
    const uint32_t table = bus_read_u32(kSlotTablePtr);
    int count = static_cast<int>(bus_read_u32(kSlotCount));
    if (table == 0 || count <= 0) return -1;
    if (count > kMaxSlots) count = kMaxSlots;
    Projector p;
    p.Load();
    int best = -1;
    double best_score = 1.0;
    for (int slot = 0; slot < count; ++slot) {
        const int id =
            static_cast<int>(bus_read_u32(table + kSlotActor + 4u * slot));
        if (id < kFirstMonster || id >= kActorCount) continue;
        const uint32_t actor = bus_read_u32(kActorTable + 8u + 4u * id);
        if (actor == 0 || (bus_read_u16(actor) & kActorLive) == 0) continue;
        const double wx = static_cast<int32_t>(bus_read_u32(actor + kActorPos));
        const double wy =
            static_cast<int32_t>(bus_read_u32(actor + kActorPos + 4u));
        const double wz =
            static_cast<int32_t>(bus_read_u32(actor + kActorPos + 8u));
        const double h =
            static_cast<int32_t>(bus_read_u32(actor + kActorHeight));
        double ax, ay, bx, by;
        if (!p.Project(wx, wy, wz, &ax, &ay)) continue;
        if (!p.Project(wx, wy + h, wz, &bx, &by)) continue;
        const double reach =
            std::fmax(kMinReach, kReachShare * std::hypot(bx - ax, by - ay));
        const double score = SegmentDistance(x, y, ax, ay, bx, by) / reach;
        if (score <= best_score) {
            best_score = score;
            best = slot;
        }
    }
    return best;
}

using Clock = std::chrono::steady_clock;
Clock::time_point g_last_step;
unsigned g_clicks_seen = 0;
int g_pointer_x = -1, g_pointer_y = -1;
// The slot the mouse moved the cursor to, while the widget stays on the
// entry it had then (g_hover_entry); a keypad move takes over.
int g_hover_slot = -1;
uint32_t g_hover_entry = 0;
// A click chose a slot this frame: the step takes it as an A.
bool g_choose = false;

}  // namespace

// Before `mov r4, r0` at 0x0217B3B4: r0 is the target slot the list widget
// is on, which the step stores in cmd+0x1D (the slot the top-screen cursor
// shows) and chooses on A.
NDS_TITLE_HOOK void Dq9BattleTargetSlot(uint32_t* entry) {
    g_choose = false;
    const Clock::time_point now = Clock::now();
    const bool fresh = now - g_last_step > std::chrono::milliseconds(200);
    g_last_step = now;

    int cx = 0, cy = 0;
    const unsigned clicks = nds_title_top_click(&cx, &cy);
    int px = -1, py = -1;
    if (!nds_title_top_pointer(&px, &py)) px = py = -1;
    if (fresh) {
        // A new target choice: clicks and a mouse resting from before it
        // choose nothing.
        g_clicks_seen = clicks;
        g_pointer_x = px;
        g_pointer_y = py;
        g_hover_slot = -1;
        return;
    }

    if (clicks != g_clicks_seen) {
        g_clicks_seen = clicks;
        const int slot = MonsterSlotAt(cx, cy);
        if (slot >= 0) {
            *entry = static_cast<uint32_t>(slot);
            g_hover_slot = -1;
            g_choose = true;
            return;
        }
    }

    const bool moved = px != g_pointer_x || py != g_pointer_y;
    g_pointer_x = px;
    g_pointer_y = py;
    if (moved && px >= 0 && Dq9MouseFriendlyMenus()) {
        const int slot = MonsterSlotAt(px, py);
        if (slot >= 0) {
            g_hover_slot = slot;
            g_hover_entry = *entry;
        }
    }
    if (g_hover_slot < 0) return;
    if (*entry != g_hover_entry) {
        g_hover_slot = -1;
        return;
    }
    *entry = static_cast<uint32_t>(g_hover_slot);
}

// Before `mov r8, r0` at 0x0217B3C8: r0 is non-zero on a new A (keys 0x601),
// which chooses the slot. A click on a monster counts as one.
NDS_TITLE_HOOK void Dq9BattleTargetChoose(uint32_t* pressed) {
    if (!g_choose) return;
    g_choose = false;
    *pressed = 1u;
}
