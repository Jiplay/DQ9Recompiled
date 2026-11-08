// Skip the boot logos and the opening movie by steering two of the game's
// own branches onto the paths it already takes after a soft reset. Each
// handler only changes the register a compare is about to test; the game's
// code does the rest. See config/hooks/dq9_arm9_ov020.toml and
// config/hooks/dq9_arm9.toml for the guest code behind each site.
#include "title_hooks.h"

// Before `cmp r0, #0` at 0x0218BF38 (overlay 20): r0 is the boot phase.
NDS_TITLE_HOOK void Dq9SkipBootLogos(uint32_t* phase) {
    if (*phase == 0u) *phase = 1u;
}

// Before `cmp r1, #0` at 0x02000FD8 (main loop): r1 is the
// "skip the opening movie" byte.
NDS_TITLE_HOOK void Dq9SkipOpeningMovie(uint32_t* skip_movie) {
    *skip_movie = 1u;
}
