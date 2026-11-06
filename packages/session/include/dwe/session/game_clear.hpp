#pragma once

#include <dwe/core/error.hpp>
#include <dwe/session/battle_registry.hpp>
#include <dwe/session/story_scenes.hpp>

#include <optional>
#include <string>

namespace dwe::session {

class BattleAdapter;
class GameSession;

// The record the ending gives (main_menu.c: awardMachinedramonData): the partner as the save
// machine registers it, with the gift's species and techniques; a technique the gift leaves out is
// none.
[[nodiscard]] RegisteredPartner endingGiftRecord(RegisteredPartner partner, const EndingGift& gift);

// What the ending's save does to the game before its question (main_menu.c: gameClearSave): the
// game counts as finished (MAIN_STATE 2) and its saves continue on `continueMap`
// (getFileCityTopMap, or the current map without one), and the gift's record goes into the
// registry. Without a gift, or when the partner cannot be read, no record is given; the game is
// finished all the same.
core::Result<void> clearGame(GameSession& session, BattleRegistry& registry,
                             const BattleAdapter& battle, const std::optional<EndingGift>& gift,
                             std::optional<std::string> continueMap);

} // namespace dwe::session
