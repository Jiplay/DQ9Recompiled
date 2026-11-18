// Zoom indoors (General > Zoom indoors, [title] zoom_indoors in
// settings.toml, off by default): Zoom and the chimaera wing take the party
// to the chosen place from under a roof too, instead of the party banging
// its head on the ceiling. Places where the game does not let them work at
// all stay as they are. See config/hooks/dq9_arm9_ov002.toml for the guest
// code.
#include "title_hooks.h"
#include "title_settings.h"

namespace {

constexpr const char* kChoices[] = {"Off", "On"};

constexpr NdsTitleSetting kZoomIndoors = {
    "zoom_indoors",
    "Zoom indoors",
    "Lets Zoom and the chimaera wing work under a roof too, instead of the "
    "party banging its head on the ceiling.",
    0, 1, 1,
    kChoices, sizeof(kChoices) / sizeof(kChoices[0]), nullptr,
    0,
};

const int kZoomIndoorsId = nds_title_setting_register(&kZoomIndoors);

// The current map's Zoom kind (map info +14, bits 0-1).
constexpr uint32_t kZoomUnderRoof = 1u;
constexpr uint32_t kZoomOutdoors = 2u;

}  // namespace

// Before `cmp r7, #2` at 0x02165CDC (overlay 2): r7 is the map's Zoom kind.
NDS_TITLE_HOOK void Dq9ZoomIndoors(uint32_t* kind) {
    if (nds_title_setting_value(kZoomIndoorsId) != 0 && *kind == kZoomUnderRoof)
        *kind = kZoomOutdoors;
}
