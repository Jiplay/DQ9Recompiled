// The battle multipliers' rules for who sets them (battle_multipliers.cpp):
// alone or hosting, this machine's own settings; visiting another player's
// world, the host's, which the Options pages show locked.
#pragma once

#include <string>

namespace dq9 {

// Multiplayer is on and this machine is a guest in another player's world
// (the game's own test, 0x0202C550).
bool VisitingAnotherWorld();

// "title.mult_..." rows: the ones the host sets while visiting.
inline bool IsBattleMultiplier(const std::string& key) {
    return key.rfind("title.mult_", 0) == 0;
}

// The host's value of a multiplier row (hundredths), for a guest.
int HostMultiplier(const std::string& key);

}  // namespace dq9
