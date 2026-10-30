#pragma once

#include <dwe/game/battle/battle.hpp>
#include <dwe/game/battle/state.hpp>

#include <string_view>

namespace dwe::game::battle {

// The localisation keys of what the battle shows the player; the UI looks them up in the text
// packs. The battle itself never holds player-facing text.

// The menu label of a command: battle.command.<name>.
std::string_view commandTextKey(Command command);
// The line of an outcome: battle.outcome.<name>.
std::string_view outcomeTextKey(Outcome outcome);
// The name of a status effect: battle.status.<name>.
std::string_view statusTextKey(StatusEffect status);
// The line of the bits won.
inline constexpr std::string_view BitsWonTextKey = "battle.result.bits";
inline constexpr std::string_view ReviveCountdownTextKey = "battle.revive_countdown";

} // namespace dwe::game::battle
