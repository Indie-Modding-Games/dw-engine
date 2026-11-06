#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/session/session_system.hpp>
#include <dwe/session/story_map_edits.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::session {

class BattleAdapter;
class CalendarAdapter;
class CardsAdapter;
class FieldItemsAdapter;
class InventoryAdapter;
class StoryAdapter;

// The ruleset section of the story orders that change the game (rules/<id>/story_values.yaml).
inline constexpr std::string_view StoryValuesSection = "story_values";

// The stats the scripts read and write, by the number they give them (script.h: SCRIPT_STAT_*;
// script_value.c: getStatsPointer).
enum class ScriptStat : std::uint8_t {
  Offense,
  Defense,
  Speed,
  Brain,
  MaxHp,
  MaxMp,
  CurrentHp,
  CurrentMp,
  Tiredness,
  Happiness,
  Discipline,
  Energy,
  Virus,
  Lifetime,
  Merit,
  StartedBattles,
  FledBattles,
  TournamentsWon,
  TournamentWins,
  TournamentsLost,
  Weight,
  TamerLevel,
  Lives,
};
inline constexpr std::size_t ScriptStatCount = static_cast<std::size_t>(ScriptStat::Lives) + 1;

// The numbers of the story_values section.
struct StoryValueRules {
  // The highest value of each stat a set_stat or an add_stat leaves (script_value.c:
  // STATS_LIMIT_ARRAY); the current HP and MP stop at their maximum instead.
  std::array<std::int32_t, ScriptStatCount> statLimits{};
  // reduce_stat stops there for the happiness and at 0 for every other stat.
  std::int32_t happinessFloor = 0;
  // A date of four story variables past this many years becomes `dateOverflow` (script_interp.c:
  // secondsToDate).
  std::int32_t dateYearLimit = 0;
  ruleset::GameTime dateOverflow{};
};

core::Result<StoryValueRules> readStoryValueRules(const ruleset::Section& section);

struct StoryGameOrdersDesc {
  // The mounted packs and the systems the orders change; all must outlive the orders.
  const assets::VirtualFileSystem* files = nullptr;
  StoryAdapter* story = nullptr;
  InventoryAdapter* inventory = nullptr;
  BattleAdapter* battle = nullptr;
  // A date a script moves to another day runs the calendar's new day; the field's items take the
  // items and chests the scripts lay.
  CalendarAdapter* calendar = nullptr;
  FieldItemsAdapter* fieldItems = nullptr;
  // The player's cards, which give_card and take_card change.
  CardsAdapter* cards = nullptr;
};

struct StoryGameContext;

// The story orders that change the game besides the story's own state (docs/reference/scripts.md,
// "Objetos, dinero, técnicas, cartas y estadísticas" and "Disparadores, variables y fecha"): the
// bag and the money, the partner's stats, conditions and moves, the date, and the map's objects
// and collision. It publishes them in the story's script VM the first time it ticks after the
// story built it, and points them at the session it ticks with. It ticks before the story, so the
// orders a section runs see this tick's session.
class StoryGameOrders final : public SessionSystem {
public:
  explicit StoryGameOrders(StoryGameOrdersDesc desc);
  ~StoryGameOrders() override;
  StoryGameOrders(const StoryGameOrders&) = delete;
  StoryGameOrders& operator=(const StoryGameOrders&) = delete;
  StoryGameOrders(StoryGameOrders&&) = delete;
  StoryGameOrders& operator=(StoryGameOrders&&) = delete;

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;
  core::Result<void> startNewGame(GameSession& session) override;
  // Binds the session and publishes the orders once.
  void tick(GameSession& session, const ruleset::ClockAdvance& advance) override;

  // The map edits the orders made since the last call, in order, for the field to apply to the map
  // it shows (story_map_edits.hpp). Edits made while no field is shown are dropped.
  [[nodiscard]] std::vector<StoryMapEdit> takeMapEdits();
  // The damage deal_damage did to the partner since the last call, oldest first, for the field to
  // show over the partner (script_interp.c:1989: addEntityText).
  [[nodiscard]] std::vector<std::int32_t> takePartnerDamage();

private:
  void bind(GameSession& session);

  std::unique_ptr<StoryGameContext> context_;
  bool published_ = false;
};

} // namespace dwe::session
