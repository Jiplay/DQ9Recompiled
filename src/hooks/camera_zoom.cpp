// Camera distance: moves DQ9's scene camera closer to or farther from what
// it looks at. Game > Camera zoom picks Auto or Manual ([title] camera_mode
// in settings.toml). Manual uses the percentage in Game > Manual camera
// distance ([title] camera_distance, saved in any mode), which the mouse
// wheel, the gamepad's right stick (up/down) and a two-finger pinch on a
// touchscreen change while Manual is on.
// 100% leaves the game untouched. See config/hooks/dq9_arm9.toml for the
// guest code.
//
// Auto follows the window: the distance grows with the height
// the main screen is shown at, so the characters keep the size they have in
// the default 2x window and a bigger window shows more around them.
//
// It applies while walking around: field, towns and dungeons. Battles,
// field cutscenes and the full-screen pages keep the game's own distance
// (see TargetCameraDistancePermille). The far plane is not moved, so very
// distant scenery can clip sooner when zoomed out.
//
// Moving the camera back also costs depth precision: Z-buffer depth goes as
// near / distance^2, so at 2x a decal lifted off a roof or a wall is a
// quarter as far from it in depth, and the depth test starts hiding parts of
// it (visible from about 150%). Dq9CameraDistanceNear moves the near plane
// out with the camera, which gives that precision back.
//
// Moving the camera back pushes every pixel deeper, so the hardware fog
// (a fixed depth curve from the map's presets) would thicken over the whole
// scene. Dq9CameraDistanceFog reshapes that curve so each pixel gets the fog
// it would have had at 100%.
#include <algorithm>
#include <cmath>
#include <chrono>

#include "title_hooks.h"
#include "title_settings.h"

#include "dq9_scene.h"

namespace {

constexpr int kAuto = 0;
constexpr int kManual = 1;
constexpr const char* kModeChoices[] = {"Auto", "Manual"};
constexpr int kModeValues[] = {kAuto, kManual};

constexpr NdsTitleSetting kCameraMode = {
    "camera_mode",
    "Camera zoom",
    "Auto follows the window size. Manual uses the distance set with the "
    "mouse wheel or the right stick.",  // at most three lines in the pages
    0, 1, 1,
    kModeChoices, sizeof(kModeChoices) / sizeof(kModeChoices[0]), kModeValues,
    kAuto,
};

// Only used in Manual mode, but always kept (and saved).
constexpr NdsTitleSetting kCameraDistance = {
    "camera_distance",
    "Manual camera distance",
    "How far the camera sits from what it looks at, in Manual zoom mode. "
    "Above 100% zooms out and shows more around you. Not in the menu.",
    1000, 4000, 1,  // thousandths of the game's own distance
    nullptr, 0, nullptr,
    1000,
    true,  // hidden: the mouse wheel and the right stick change it
};

const int kCameraModeId = nds_title_setting_register(&kCameraMode);
const int kCameraDistanceId = nds_title_setting_register(&kCameraDistance);
const bool kZoomBound = (nds_title_set_zoom_binding(kCameraDistanceId,
                                                    kCameraModeId, kManual),
                         true);

// Auto: 100% when the main screen is shown 2x (the default window), and
// proportional to its height, within the fixed choices' range.
constexpr int kAutoReferencePoints = 192 * 2;
constexpr int kAutoMinPermille = 1000;
constexpr int kAutoMaxPermille = 4000;

// The distance to use, in thousandths of the game's own (1000 = untouched).
// Headless (no window) Auto stays at 1000.
//
// Full-screen pages (dq9::FullScreenPage: the battle records, the Defeated
// Monster List, Attributes, the equipment screen, character creation) stay
// at 1000 too. They are shown stretched as on the DS, and their camera
// frames one model (Stella, the monster, the character) beside the 2D
// layout: moving it back shrinks the model toward the screen centre, away
// from the place the page keeps for it.
// Battles and field cutscenes (dq9::FieldCutscene: story events, whose
// camera the script frames) stay at 1000 as well: the zoom is for walking
// around.
int TargetCameraDistancePermille() {
    if (dq9::FieldCutscene()) return 1000;
    if (nds_title_setting_value(kCameraModeId) == kManual)
        return std::max(nds_title_setting_value(kCameraDistanceId), 1000);
    const int points = nds_title_display_height();
    if (points <= 0) return 1000;
    const int permille = static_cast<int>(
        (static_cast<int64_t>(points) * 1000 + kAutoReferencePoints / 2) /
        kAutoReferencePoints);
    return permille < kAutoMinPermille   ? kAutoMinPermille
           : permille > kAutoMaxPermille ? kAutoMaxPermille
                                         : permille;
}

// The distance follows its target smoothly, so the camera glides in when
// an event starts and back out when it ends, and with the mouse wheel.
// Battles and full-screen pages switch at once: their scenes start on a
// camera of their own, which must not open zoomed out.
constexpr double kEaseSeconds = 0.2;  // time constant

double g_permille = 1000;

// Once per camera build, at its first hook (Dq9CameraDistanceNear), so the
// near plane, the dolly and the fog of a frame all use the same distance.
void UpdateCameraDistance() {
    using Clock = std::chrono::steady_clock;
    static Clock::time_point last{};
    static bool last_instant = true;
    const Clock::time_point now = Clock::now();
    const double dt = std::chrono::duration<double>(now - last).count();
    last = now;
    const bool instant = dq9::InBattle() || dq9::FullScreenPage();
    if (instant) {
        g_permille = 1000;
    } else {
        const int target = TargetCameraDistancePermille();
        if (last_instant || dt <= 0 || dt > 1.0) {
            g_permille = target;
        } else {
            g_permille += (target - g_permille) * (1 - std::exp(-dt / kEaseSeconds));
            if (std::fabs(target - g_permille) < 1) g_permille = target;
        }
    }
    last_instant = instant;
}

int CameraDistancePermille() { return static_cast<int>(std::lround(g_permille)); }

// Frame offsets in 0x0202E0B4 (sub sp, sp, #0x150 after push {r3-r5, lr}).
constexpr uint32_t kPositionOffset = 0xFC;
constexpr uint32_t kTargetOffset = 0xF0;

// What 0x0202E0B4 publishes: the projection (MtxFx44, row-major) and the
// camera position and target the view matrix is built from.
constexpr uint32_t kProjection = 0x0210A018;
constexpr uint32_t kCameraPosition = 0x0210A250;
constexpr uint32_t kCameraTarget = 0x0210A268;

// Fog fields of the environment block in 0x02051D38 (r7), filled every
// frame by 0x02051254 and passed to G3X_SetFog and the density table load.
constexpr uint32_t kFogShiftOffset = 0x54;
constexpr uint32_t kFogDepthOffset = 0x58;
constexpr uint32_t kFogTableOffset = 0x64;

constexpr int kFogEntries = 32;
constexpr int kMaxFogShift = 10;

// Z-buffer depth (15 bits, as FOG_OFFSET counts it) of a point at view
// distance `dist`, for a projection with z_ndc = a - b / dist: the hardware
// stores (z / w) * 0x4000 + 0x3FFF.
struct DepthMap {
    double a, b;
    double depth(double dist) const {
        if (!std::isfinite(dist)) return 0x4000 * a + 0x3FFF;
        return 0x4000 * (a - b / dist) + 0x3FFF;
    }
    double distance(double depth) const {
        const double denom = a - (depth - 0x3FFF) / 0x4000;
        return denom > 0 ? b / denom : INFINITY;
    }
};

// The hardware's density at `depth`: entry k sits at offset + (k + 1)
// steps, linear in between, the first entry before and the last after.
double FogDensity(const uint8_t* table, double offset, int shift,
                  double depth) {
    double t = (depth - offset) * (1 << shift) / 0x400;
    if (t <= 1) return table[0];
    if (t >= kFogEntries) return table[kFogEntries - 1];
    const int k = static_cast<int>(t);
    const double frac = t - k;
    return table[k - 1] * (1 - frac) + table[k] * frac;
}

// Camera fields read when the projection is built (0x0202E138).
constexpr uint32_t kCamPosition = 0x120;
constexpr uint32_t kCamTarget = 0x12C;

// How far the near plane may move out: this share of the dolly. Whatever
// was in front of the original camera is at least the dolly farther from
// the moved one, so nothing that was drawn gets cut by the near plane.
constexpr double kNearShareOfDolly = 0.5;

// The near plane for a camera moved to `k` times its distance `target` from
// what it looks at (world units, k > 1). Depth precision at the target goes
// as near / distance^2, so k^2 times the near plane keeps it as it was at
// 100%, as far as the dolly allows.
double MovedNear(double near, double k, double target) {
    return std::min(near * k * k, near + kNearShareOfDolly * (k - 1) * target);
}

// The inverse: the game's near plane from the moved one.
double OriginalNear(double moved, double k, double target) {
    const double scaled = moved / (k * k);
    if (scaled * k * k <= scaled + kNearShareOfDolly * (k - 1) * target)
        return scaled;
    return moved - kNearShareOfDolly * (k - 1) * target;
}

}  // namespace

// Before `bl MTX_PerspectiveW` at 0x0202E138 (r3 = near, r4 = camera): the
// projection with the near plane moved out with the camera (see
// MovedNear). The far plane and the field of view are the game's.
NDS_TITLE_HOOK void Dq9CameraDistanceNear(uint32_t* r3, uint32_t* r4) {
    UpdateCameraDistance();
    const int permille = CameraDistancePermille();
    if (permille <= 1000) return;
    const double k = permille / 1000.0;
    double target = 0;
    for (uint32_t axis = 0; axis < 3; ++axis) {
        const double d =
            double(static_cast<int32_t>(bus_read_u32(*r4 + kCamPosition + axis * 4u))) -
            static_cast<int32_t>(bus_read_u32(*r4 + kCamTarget + axis * 4u));
        target += d * d;
    }
    target = std::sqrt(target) / 4096.0;
    const double near = static_cast<int32_t>(*r3) / 4096.0;
    if (near <= 0 || target <= 0) return;
    const double moved = MovedNear(near, k, target);
    *r3 = static_cast<uint32_t>(static_cast<int32_t>(std::lround(moved * 4096.0)));
}

// Before `ldr r0, =0x0210A250` at 0x0202E4F8: sp+0xFC is the camera
// position and sp+0xF0 its target, three fx32 words each.
NDS_TITLE_HOOK void Dq9CameraDistance(uint32_t* sp) {
    const int permille = CameraDistancePermille();
    if (permille == 1000) return;
    for (uint32_t axis = 0; axis < 3; ++axis) {
        const uint32_t position_addr = *sp + kPositionOffset + axis * 4u;
        const int32_t position =
            static_cast<int32_t>(bus_read_u32(position_addr));
        const int32_t target = static_cast<int32_t>(
            bus_read_u32(*sp + kTargetOffset + axis * 4u));
        const int64_t offset =
            (static_cast<int64_t>(position) - target) * permille / 1000;
        bus_write_u32(position_addr, static_cast<uint32_t>(target + offset));
    }
}

namespace {

// Map environment (r7 = block, *block = env) fog presets: env+0x410 is the
// fog mode (1 fixed, 2 by time of day), the presets are 0x38 bytes from
// env+0x288 and are laid out like the per-frame fog at block+0x4C.
constexpr uint32_t kEnvFogMode = 0x410;
constexpr uint32_t kEnvFogPresets = 0x288;
constexpr uint32_t kFogPresetSize = 0x38;
constexpr uint32_t kFogPresetCount = 7;
constexpr uint32_t kBlockFog = 0x4C;

}  // namespace

// Before `bl G3X_SetFog` at 0x02051B78, where a fixed-fog map sends its
// preset (r4) once: keep the preset's address in block+0x4C (r7), which
// only the time-of-day mode fills.
NDS_TITLE_HOOK void Dq9CameraDistanceFixedFogPreset(uint32_t* r4,
                                                    uint32_t* r7) {
    bus_write_u32(*r7 + kBlockFog, *r4);
}

// Before `beq 0x02052238` at 0x02051D64, where the per-frame code skips
// fog for fixed-fog maps (r0 = 1): copy the kept preset to block+0x50..
// and jump to the fog tail at 0x02052208, so the camera distance can
// reshape it every frame. At 100% this sends the hardware the same fog the
// map set up, which also restores it after a change back to 100%.
NDS_TITLE_HOOK bool Dq9CameraDistanceFixedFog(uint32_t* r0, uint32_t* r7) {
    if (*r0 != 1) return false;
    const uint32_t block = *r7;
    const uint32_t presets = bus_read_u32(block) + kEnvFogPresets;
    const uint32_t preset = bus_read_u32(block + kBlockFog);
    if (preset < presets || (preset - presets) % kFogPresetSize != 0 ||
        (preset - presets) / kFogPresetSize >= kFogPresetCount)
        return false;
    // Everything after the first word: mode, shift, offset, color, alpha
    // and the 32 densities.
    for (uint32_t at = 4; at < kFogPresetSize; at += 4)
        bus_write_u32(block + kBlockFog + at, bus_read_u32(preset + at));
    return true;
}

// Before `ldr r1, [r7, #0x50]` at 0x02052208, where 0x02051D38 hands the
// frame's fog to G3X_SetFog (shift r7+0x54, offset r7+0x58) and then loads
// the 32 densities at r7+0x64. 0x02051254 rewrites all of them from the
// map's presets every frame, so they can be changed in place.
//
// The dolly adds the same distance to every pixel's view depth, but Z
// depth is not linear in distance, so a new offset alone cannot undo it:
// the density table is resampled over the old curve's range as it now
// lands in depth, at the finest slope that still covers it.
NDS_TITLE_HOOK void Dq9CameraDistanceFog(uint32_t* r7) {
    // With Auto, a resize between the camera hook and this one can skew the
    // fog for that one frame.
    const int permille = CameraDistancePermille();
    if (permille == 1000) return;

    const auto word = [](uint32_t addr) {
        return static_cast<int32_t>(bus_read_u32(addr));
    };
    // Perspective only: m[2][3] = -scaleW, m[3][3] = 0.
    const int32_t m23 = word(kProjection + 11 * 4);
    if (m23 == 0 || word(kProjection + 15 * 4) != 0) return;
    const DepthMap map = {double(word(kProjection + 10 * 4)) / m23,
                          double(word(kProjection + 14 * 4)) / m23};

    double moved = 0;
    for (uint32_t axis = 0; axis < 3; ++axis) {
        const double d = double(word(kCameraPosition + axis * 4u)) -
                         word(kCameraTarget + axis * 4u);
        moved += d * d;
    }
    // The published camera is already moved: moved = permille/1000 of the
    // original distance, and every view depth grew by the difference.
    const double dolly =
        std::sqrt(moved) / 4096.0 * (permille - 1000) / permille;
    if (map.b <= 0 || map.a <= 1 || dolly == 0) return;

    // The depth map of the game's own projection: Dq9CameraDistanceNear
    // moved the near plane out (z = a - b / dist with a = (f + n) / (f - n),
    // b = 2 f n / (f - n); the far plane is the game's).
    DepthMap old_map = map;
    if (permille > 1000) {
        const double k = permille / 1000.0;
        const double far = map.b / (map.a - 1);
        const double near = OriginalNear(map.b / (map.a + 1), k,
                                         std::sqrt(moved) / 4096.0 / k);
        if (near <= 0 || near >= far) return;
        old_map = {(far + near) / (far - near), 2 * far * near / (far - near)};
    }

    const uint32_t block = *r7;
    const int shift = word(block + kFogShiftOffset) & 0xF;
    const double offset = word(block + kFogDepthOffset) & 0x7FFF;
    uint8_t table[kFogEntries];
    for (int k = 0; k < kFogEntries; ++k)
        table[k] = bus_read_u8(block + kFogTableOffset + k) & 0x7F;

    const auto to_new = [&](double depth) {
        return map.depth(old_map.distance(depth) + dolly);
    };
    const auto to_old = [&](double depth) {
        const double dist = map.distance(depth) - dolly;
        return dist > 0 ? old_map.depth(dist) : -INFINITY;
    };

    // The old curve only changes between entry 0 and entry 31.
    const double step = double(0x400 >> shift);
    const double lo = to_new(offset + step);
    const double hi = to_new(offset + kFogEntries * step);
    int new_shift = 0;
    while (new_shift < kMaxFogShift &&
           (kFogEntries - 1) * double(0x400 >> (new_shift + 1)) >= hi - lo)
        ++new_shift;
    const int new_step = 0x400 >> new_shift;
    long new_offset = std::lround(lo) - new_step;
    if (new_offset < 0) new_offset = 0;
    if (new_offset > 0x7FFF) new_offset = 0x7FFF;

    for (int k = 0; k < kFogEntries; ++k) {
        const double old_depth =
            to_old(double(new_offset) + double(k + 1) * new_step);
        const long density =
            std::lround(FogDensity(table, offset, shift, old_depth));
        bus_write_u8(block + kFogTableOffset + k,
                     static_cast<uint8_t>(density < 0     ? 0
                                          : density > 127 ? 127
                                                          : density));
    }
    bus_write_u32(block + kFogShiftOffset, static_cast<uint32_t>(new_shift));
    bus_write_u32(block + kFogDepthOffset, static_cast<uint32_t>(new_offset));
}

