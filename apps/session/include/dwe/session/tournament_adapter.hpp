#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/game/tournament/bracket_animation.hpp>
#include <dwe/game/tournament/cup_extras.hpp>
#include <dwe/game/tournament/tournament.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/map_head_lines.hpp>
#include <dwe/session/session_system.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::session {

class CardsAdapter;
class InventoryAdapter;
class MedalsAdapter;
class StoryAdapter;

struct TournamentAdapterDesc {
  // The mounted packs (data/stadium, data/digimon, data/moves and the map scripts' listings), the
  // story (the cup, the residents, the result) and the battles; all must outlive the adapter.
  const assets::VirtualFileSystem* files = nullptr;
  StoryAdapter* story = nullptr;
  BattleAdapter* battle = nullptr;
  // The bag a ruleset's extra prizes go to and the cards the tournaments' medal counts; without
  // them there are no extra prizes and no medal for the cards.
  InventoryAdapter* inventory = nullptr;
  CardsAdapter* cards = nullptr;
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
  // The story variable that holds the cup entered (pstat 3); 0 without rules.
  [[nodiscard]] std::int32_t cupVariable() const { return rules_ ? rules_->cupVariable : 0; }
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

  // The registration at the counter (story routine 18: dget/dget.c: initTournamentSchedule).
  [[nodiscard]] const game::tournament::ScheduleRules* scheduleRules() const {
    return schedule_ ? &*schedule_ : nullptr;
  }
  // Whether the partner is registered already (trigger 37): the routine then ends at once.
  [[nodiscard]] bool registered() const;
  // The five days' schedule from today on the active map's script (fillEnabledTournamentTable),
  // with what the partner may enter; clears the cup variable (writePStat(3, 0xFF)).
  core::Result<game::tournament::Schedule> openSchedule(GameSession& session);
  // A cup's grade mark (TOURNAMENT_GRADES) and the key of its text in the active map's script (the
  // show_textbox of its entry 0, initTournamentInfo); empty when unknown.
  [[nodiscard]] std::string grade(std::int32_t cup) const;
  [[nodiscard]] std::string cupTextKey(std::int32_t cup) const;
  // The key of a MAPHEAD line of the registration's section; empty when unknown.
  [[nodiscard]] std::string lineKey(std::int32_t line);
  // The cup picked (tickTournamentSchedule: writePStat(3, cup)).
  core::Result<void> chooseCup(std::int32_t cup);
  // The partner too strong for the cup picked (initTournamentSchedule state 3): trigger 40 set or
  // cleared.
  core::Result<void> markOverlevel(bool overlevel);
  // The entry (state 5): the registration's triggers, the day (the column picked: 0 today) and the
  // partner's species.
  core::Result<void> enter(GameSession& session, std::size_t column);

  // The arena of a cup entered on this map (STD_initializeTournament, STD_loadArenaAssets): its
  // model, its collision grid, where the fighters stand and its camera.
  struct ArenaStage {
    std::string model;
    assets::MapCollision collision;
    game::tournament::ArenaStart start;
    // How far each fighter runs in during the intro, to its start (STD_runIntro).
    std::int32_t walkIn = 0;
    std::int32_t partnerYaw = 0;
    std::int32_t rivalYaw = 0;
    game::tournament::ArenaCamera camera;
  };
  core::Result<ArenaStage> arenaStage(std::string_view map) const;

  // The bracket the cup shows before each match (data/stadium's tables and tournament.yaml's
  // bracket); nullptr for an extraction or a ruleset without them.
  [[nodiscard]] const game::tournament::BracketLayout* bracketLayout() const {
    return bracketLayout_ ? &*bracketLayout_ : nullptr;
  }
  [[nodiscard]] const game::tournament::BracketRules* bracketRules() const {
    return bracketRules_ ? &*bracketRules_ : nullptr;
  }
  // The cup entered's grade card (STD_GRADES); empty when unknown.
  [[nodiscard]] std::string cardImage() const;
  // The id of the species with the original's number; empty when unknown.
  [[nodiscard]] std::string speciesId(std::int32_t number) const;

  // Whether the cup was ever won (its trigger, saved with the story's).
  [[nodiscard]] bool cupWon(std::int32_t cup) const;
  // The ruleset's extra prizes of a cup (tournament.yaml: extra_prizes).
  [[nodiscard]] std::vector<game::tournament::ExtraPrize> extraPrizes(std::int32_t cup) const {
    return game::tournament::extraPrizesOf(extraPrizes_, cup);
  }

  // The medals the tournaments' medal is given with (created after this adapter).
  void attachMedals(MedalsAdapter* medals) { medals_ = medals; }
  // Story routine 22 (tournament.c: scriptCheckTournamentMedal): the medal earned, if any, given
  // now, and the key of its line (MAPHEAD, no speaker); nothing when none is earned.
  core::Result<std::optional<std::string>> awardMedal(GameSession& session);

private:
  [[nodiscard]] const assets::DigimonRow* rowOfNumber(std::int32_t number) const;
  // The active map's script listing and its number.
  core::Result<std::pair<std::uint16_t, core::YamlNode>> mapListing() const;
  // The key of a line of a MAPHEAD section; empty when unknown.
  [[nodiscard]] std::string sectionLineKey(std::int32_t section, std::int32_t line);
  // Gives a won cup's extra prizes.
  void giveExtraPrizes(std::int32_t cup);

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
  std::optional<game::tournament::ArenaRules> arena_;
  std::optional<game::tournament::ScheduleRules> schedule_;
  std::optional<game::tournament::TournamentTable> table_;
  std::optional<MapHeadLineTables> lineTables_;
  // The cups' text keys of the schedule opened last, by cup.
  std::map<std::int32_t, std::string> cupTexts_;
  std::optional<game::tournament::BracketLayout> bracketLayout_;
  std::optional<game::tournament::BracketRules> bracketRules_;
  std::vector<std::string> stadiumGrades_;
  std::vector<game::tournament::ExtraPrize> extraPrizes_;
  std::optional<game::tournament::TournamentMedalRules> medalRules_;
  MedalsAdapter* medals_ = nullptr;
};

} // namespace dwe::session
