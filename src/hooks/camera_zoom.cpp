// Camera distance: moves DQ9's scene camera closer to or farther from what
// it looks at, by the percentage set in the runtime menu (Game > Camera
// distance, [title] camera_distance in settings.toml). 100% leaves the game
// untouched. See config/hooks/dq9_arm9.toml for the guest code.
//
// It applies to every scene that uses the main 3D camera: field, towns,
// dungeons, battles and cutscenes. The far plane is not moved, so very
// distant scenery can clip sooner when zoomed out.
#include "title_hooks.h"
#include "title_settings.h"

namespace {

constexpr const char* kChoices[] = {"50%",  "75%",  "100%", "125%",
                                    "150%", "175%", "200%", "250%",
                                    "300%", "400%"};
constexpr int kValues[] = {50, 75, 100, 125, 150, 175, 200, 250, 300, 400};

constexpr NdsTitleSetting kCameraDistance = {
    "camera_distance",
    "Camera distance",
    "How far the camera sits from what it looks at. Above 100% zooms out "
    "and shows more around you; below zooms in.",
    50, 400, 25,
    kChoices, sizeof(kChoices) / sizeof(kChoices[0]), kValues,
    100,
};

const int kCameraDistanceId = nds_title_setting_register(&kCameraDistance);

// Frame offsets in 0x0202E0B4 (sub sp, sp, #0x150 after push {r3-r5, lr}).
constexpr uint32_t kPositionOffset = 0xFC;
constexpr uint32_t kTargetOffset = 0xF0;

}  // namespace

// Before `ldr r0, =0x0210A250` at 0x0202E4F8: sp+0xFC is the camera
// position and sp+0xF0 its target, three fx32 words each.
NDS_TITLE_HOOK void Dq9CameraDistance(uint32_t* sp) {
    const int percent = nds_title_setting_value(kCameraDistanceId);
    if (percent == 100) return;
    for (uint32_t axis = 0; axis < 3; ++axis) {
        const uint32_t position_addr = *sp + kPositionOffset + axis * 4u;
        const int32_t position =
            static_cast<int32_t>(bus_read_u32(position_addr));
        const int32_t target = static_cast<int32_t>(
            bus_read_u32(*sp + kTargetOffset + axis * 4u));
        const int64_t offset =
            (static_cast<int64_t>(position) - target) * percent / 100;
        bus_write_u32(position_addr, static_cast<uint32_t>(target + offset));
    }
}
