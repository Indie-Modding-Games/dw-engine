#pragma once

#include <dwe/game/field/script_warps.hpp>

namespace dwe::session {

class GameSession;
class InventoryAdapter;
class StoryAdapter;

// What the field's script warps read of the game (docs/architecture/warps-de-script.md), live: the
// story's triggers, the bag's item counts and the money (none without an inventory), and the
// partner's stats and conditions while a game is played. The session and the adapters must outlive
// the field.
game::field::GuardFacts fieldGuardFacts(const GameSession& session,
                                        const InventoryAdapter* inventory,
                                        const StoryAdapter& story);

// Where the field writes the triggers a script warp it takes sets: the story.
game::field::TriggerWriter fieldTriggerWriter(StoryAdapter& story);

} // namespace dwe::session
