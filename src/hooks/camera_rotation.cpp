// Unlock camera rotation: lets L and R turn DQ9's field camera all the way
// round instead of stopping 45 degrees either side of the default (Game >
// Unlock camera rotation, [title] unlock_camera_rotation in settings.toml).
// Off leaves the game untouched; L+R still resets the camera. See
// config/hooks/dq9_arm9.toml for the guest code.
#include "title_hooks.h"
#include "title_settings.h"

namespace {

constexpr const char* kChoices[] = {"Off", "On"};

constexpr NdsTitleSetting kUnlockRotation = {
    "unlock_camera_rotation",
    "Unlock camera rotation",
    "Lets L and R turn the field camera all the way round. Off, it stops "
    "45 degrees either side, as in the original. L+R still resets it.",
    0, 1, 1,
    kChoices, sizeof(kChoices) / sizeof(kChoices[0]), nullptr,
    0,
};

const int kUnlockRotationId = nds_title_setting_register(&kUnlockRotation);

}  // namespace

// Before `cmp r6, r1` at 0x020A2364 (L) and 0x020A23C8 (R): true jumps
// past the yaw clamp to the call that sets the yaw.
NDS_TITLE_HOOK bool Dq9UnlockCameraRotation() {
    return nds_title_setting_value(kUnlockRotationId) != 0;
}
