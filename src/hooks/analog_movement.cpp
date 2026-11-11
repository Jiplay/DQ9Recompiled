// Analog movement: the hero walks the way the gamepad's left stick points,
// at any angle, instead of the eight directions of the D-pad the frontend
// maps it to. With the stick at rest (the D-pad itself, the keyboard) the
// game is untouched. See config/hooks/dq9_arm9.toml for the guest code.
#include <cmath>
#include <cstdint>

#include "title_hooks.h"
#include "title_settings.h"

namespace {

// Angles are fx12 radians, wrapped to [0, 2pi) by 0x02030F40.
constexpr int32_t kTwoPi = 0x6488;
// The D-pad angle of each direction code 8..15 (up, down, left, right,
// up-left, up-right, down-left, down-right), from 0 = down towards right.
constexpr uint32_t kDpadAngles = 0x020E781C;
// The stick has to be past this to take over: the frontend maps it to the
// D-pad from 0.5 on each axis (0.35 once held), so the game is moving by
// then, and a resting stick leaves the D-pad alone.
constexpr float kMinDeflection = 0.3f;

}  // namespace

// Before `cmp r3, r2` at 0x020383F0, in the hero's D-pad movement: r2 is
// the direction code (8..15), r6 the angle the hero is about to walk at, the
// code's angle plus the camera yaw, wrapped. True jumps past the snap to
// the nearby path's direction (stored only for the code, and the stick has
// none), with r6 the stick's angle plus the same yaw.
NDS_TITLE_HOOK bool Dq9AnalogMovement(uint32_t* code, uint32_t* angle) {
    int sx = 0, sy = 0;
    if (!nds_title_left_stick(&sx, &sy)) return false;
    const float x = static_cast<float>(sx) / 32767.0f;
    const float y = static_cast<float>(sy) / 32767.0f;
    if (x * x + y * y < kMinDeflection * kMinDeflection) return false;
    if (*code < 8 || *code > 15) return false;

    const int32_t dpad =
        static_cast<int32_t>(bus_read_u32(kDpadAngles + 4 * (*code - 8)));
    const int32_t yaw = static_cast<int32_t>(*angle) - dpad;
    // 0 = down, pi/2 = right: atan2(x, y) with y pointing down.
    const float stick = std::atan2(x, y);
    const int32_t turn = static_cast<int32_t>(
        std::lround(stick * static_cast<float>(kTwoPi) / 6.2831853f));
    int32_t wrapped = (yaw + turn) % kTwoPi;
    if (wrapped < 0) wrapped += kTwoPi;
    *angle = static_cast<uint32_t>(wrapped);
    return true;
}
