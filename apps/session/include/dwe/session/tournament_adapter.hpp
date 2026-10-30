#pragma once

#include <dwe/game/battle/battle.hpp>
#include <dwe/game/tournament/tournament.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/session_system.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::session {

class StoryAdapter;

struct TournamentAdapterDesc {
  // The mounted packs (data/stadium, data/digimon, data/moves and the map scripts' listings), the
  // story (the cup, the residents, the result) and the battles; all must outlive the adapter.
  const assets::VirtualFileSystem* files = nullptr;
  StoryAdapter* story = nullptr;
  BattleAdapter* battle = nullptr;
};

// A tournament the story starts (call_routine 6: tournament.c: scriptStartTournament, utils2.c:
// startTournament, std_setup.c: STD_tournamentMain): the cup entered, its rivals and bracket, the
// three matches as battles, the partner's recovery between them and, at the end, the record, the
// cup's trigger and the wins the script reads. The app plays the matches on its battle screen.
class TournamentAdapter final : public SessionSystem {
public:
  explicit TournamentAdapter(TournamentAdapterDesc desc) : desc_{desc} {}

  core::Result<void> readRules(const ruleset::Ruleset& rules) override;

  // Whether the ruleset has tournaments.
  [[nodiscard]] bool enabled() const { return rules_.has_value(); }
  // Starts the cup in the cup variable on the active map's script: draws its rivals and its
  // bracket. Fails (logged by the caller) when there are no rules, cup or rivals.
  core::Result<void> begin(GameSession& session);
  [[nodiscard]] bool running() const { return bracket_.has_value(); }
  [[nodiscard]] const game::tournament::Bracket* bracket() const {
    return bracket_ ? &*bracket_ : nullptr;
  }
  [[nodiscard]] std::int32_t cup() const { return cup_; }
  // The round being played (0 to 2) and the matches won so far.
  [[nodiscard]] std::size_t round() const { return round_; }
  [[nodiscard]] std::int32_t wins() const { return wins_; }
  // The next match as a battle (STD_initializeOpponent, STD_combatMain): the round's rival, the
  // clock, no fleeing; `partner` and `tamer` stand where the fight starts.
  core::Result<BattleRequest> matchRequest(GameSession& session, game::battle::Point partner,
                                           game::battle::Point tamer, std::string map,
                                           game::battle::Point rival);
  // A match's end, after the battle adapter gave it to the partner: the partner gets a fifth of its
  // maximum HP and MP back (STD_deinitializeCombat). Whether another match follows.
  bool matchOver(GameSession& session, game::battle::Outcome outcome);
  // The tournament's end (startTournament): the record, the cup's trigger when it is won, the
  // registration cleared and the wins in the result variable. The wins.
  core::Result<std::int32_t> finish(GameSession& session);

private:
  [[nodiscard]] const assets::DigimonRow* rowOfNumber(std::int32_t number) const;

  TournamentAdapterDesc desc_;
  std::optional<game::tournament::TournamentRules> rules_;
  // The record's highest values (story_values: enforceStatsLimits 0x11 to 0x13).
  std::int32_t wonLimit_ = 0;
  std::int32_t winsLimit_ = 0;
  std::int32_t lostLimit_ = 0;
  std::optional<game::tournament::Bracket> bracket_;
  std::int32_t cup_ = 0;
  std::size_t round_ = 0;
  std::int32_t wins_ = 0;
  bool lost_ = false;
};

} // namespace dwe::session
