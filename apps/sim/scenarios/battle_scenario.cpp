// The battle scenario: plays one battle per run between a partner and one to three enemies of the
// ruleset's Digimon table, with no commands from the tamer, and counts how it ended
// (docs/guides/simulaciones.md). The mean of each outcome is its rate over the runs. The fighters'
// stats are made up from their level unless the parameters give them.

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/move_table.hpp>
#include <dwe/core/error.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/game/battle/combatant.hpp>
#include <dwe/game/battle/moves.hpp>
#include <dwe/game/battle/rules.hpp>
#include <dwe/game/battle/simulation.hpp>
#include <dwe/game/battle/strategies.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/sim/scenario.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace dwe::sim {

namespace {

namespace battle = game::battle;

constexpr std::string_view BattleSection = "battle";
constexpr std::string_view DigimonRole = "digimon";
constexpr std::string_view MovesRole = "moves";

constexpr std::string_view PartnerParameter = "partner";
constexpr std::string_view EnemiesParameter = "enemies";
constexpr std::string_view StatsParameter = "stats";
constexpr std::string_view HpParameter = "hp";
constexpr std::string_view MpParameter = "mp";
constexpr std::string_view DisciplineParameter = "discipline";
constexpr std::string_view FrameLimitParameter = "frame_limit";
constexpr char ListSeparator = ',';

constexpr std::string_view VictoryMetric = "victory";
constexpr std::string_view DefeatMetric = "defeat";
constexpr std::string_view FledMetric = "fled";
constexpr std::string_view UnfinishedMetric = "unfinished";
constexpr std::string_view SecondsMetric = "seconds";
constexpr std::string_view PartnerHpMetric = "partner_hp_percent";

// Made-up stats when the parameters give none: offense, defense, speed and brain of 100 a level,
// three times as much HP, and MP out of reach: in the original a fighter out of MP waits for good,
// so two of them never end the battle.
constexpr std::int32_t StatsPerLevel = 100;
constexpr std::int32_t HpPerStat = 3;
constexpr std::int32_t OutOfReachMp = 9999;
constexpr std::int32_t DefaultDiscipline = 50;
// The usual move priorities of an enemy: the three techniques alike, never the finisher slot.
constexpr std::int32_t MovePriority = 10;
// Where the fighters stand: the partner at the origin, the enemies in a row in front of it.
constexpr std::int32_t EnemyDistance = 900;
constexpr std::int32_t EnemySpacing = 300;
constexpr double Hundred = 100.0;

template <typename T>
core::Result<T> parsed(std::string_view key, std::string_view text) {
  T value{};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return core::fail("battle scenario: --param " + std::string{key} + " is not a number: '" +
                      std::string{text} + "'");
  }
  return value;
}

// A number parameter, or the fallback when it is not given.
template <typename T>
core::Result<std::optional<T>> numberParameter(const ScenarioParameters& parameters,
                                               std::string_view key) {
  const auto found = parameters.find(key);
  if (found == parameters.end()) {
    return std::optional<T>{};
  }
  auto value = parsed<T>(key, found->second);
  if (!value) {
    return std::unexpected{value.error()};
  }
  return std::optional<T>{*value};
}

std::vector<std::string> splitList(std::string_view text) {
  std::vector<std::string> items;
  while (!text.empty()) {
    const std::size_t end = std::min(text.find(ListSeparator), text.size());
    if (end > 0) {
      items.emplace_back(text.substr(0, end));
    }
    text.remove_prefix(std::min(end + 1, text.size()));
  }
  return items;
}

// The stats a fighter fights with.
struct GivenStats {
  std::optional<std::int32_t> stats;
  std::optional<std::int32_t> hp;
  std::int32_t mp = OutOfReachMp;
};

class BattleScenario final : public Scenario {
public:
  core::Result<void> setup(const ScenarioContext& context) override {
    auto rules = battle::readBattleRules(*context.rules);
    if (!rules) {
      return std::unexpected{rules.error()};
    }
    rules_ = std::move(*rules);
    auto section = context.rules->requireSection(BattleSection);
    if (!section) {
      return std::unexpected{section.error()};
    }
    section_ = **section;
    // The strategies the ruleset names must exist before any run builds its own.
    if (auto strategies = battle::createStrategies(*section_); !strategies) {
      return std::unexpected{strategies.error()};
    }
    if (auto read = readParameters(*context.parameters); !read) {
      return read;
    }
    if (auto tables = loadTables(context); !tables) {
      return tables;
    }
    return buildFighters();
  }

  [[nodiscard]] core::Result<Metrics> run(std::uint64_t seed) const override {
    if (!section_ || !moves_) {
      return core::fail("battle scenario: run before setup");
    }
    auto strategies = battle::createStrategies(*section_);
    if (!strategies) {
      return std::unexpected{strategies.error()};
    }
    auto fight = battle::Battle::start(setup_, rules_, *moves_, std::move(*strategies), seed);
    if (!fight) {
      return std::unexpected{fight.error()};
    }
    while (!fight->finished() && fight->state().frame <= frameLimit_) {
      fight->step();
    }
    const std::optional<battle::BattleResult>& result = fight->result();
    const auto outcomeIs = [&result](battle::Outcome outcome) {
      return result && result->outcome == outcome ? 1.0 : 0.0;
    };
    const battle::Fighter& partner = fight->state().partner();
    const double hpLeft = Hundred * static_cast<double>(std::max(partner.hp, 0)) /
                          static_cast<double>(std::max(partner.stats.maxHp, 1));
    return Metrics{
        {.name = std::string{VictoryMetric}, .value = outcomeIs(battle::Outcome::Victory)},
        {.name = std::string{DefeatMetric}, .value = outcomeIs(battle::Outcome::Defeat)},
        {.name = std::string{FledMetric}, .value = outcomeIs(battle::Outcome::Fled)},
        {.name = std::string{UnfinishedMetric}, .value = result ? 0.0 : 1.0},
        {.name = std::string{SecondsMetric},
         .value = static_cast<double>(fight->state().frame) /
                  static_cast<double>(battle::FramesPerSecond)},
        {.name = std::string{PartnerHpMetric}, .value = hpLeft},
    };
  }

private:
  core::Result<void> loadTables(const ScenarioContext& context) {
    const assets::AssetId* digimonId = context.rules->table(DigimonRole);
    const assets::AssetId* movesId = context.rules->table(MovesRole);
    if (digimonId == nullptr || movesId == nullptr) {
      return core::fail("battle scenario: the ruleset names no digimon or moves table");
    }
    auto digimon = assets::loadDigimonTable(*context.files, *digimonId);
    if (!digimon) {
      return std::unexpected{digimon.error()};
    }
    digimon_ = std::move(digimon->digimon);
    for (const std::string& id : fighterIds()) {
      if (std::ranges::find(digimon_, id, &assets::DigimonRow::id) == digimon_.end()) {
        return core::fail("battle scenario: no species '" + id + "' in the digimon table");
      }
    }
    auto moveTable = assets::loadMoveTable(*context.files, *movesId);
    if (!moveTable) {
      return std::unexpected{moveTable.error()};
    }
    auto moves = battle::MoveCatalog::build(*moveTable, rules_);
    if (!moves) {
      return std::unexpected{moves.error()};
    }
    moves_ = std::make_unique<battle::MoveCatalog>(std::move(*moves));
    return {};
  }

  core::Result<void> readParameters(const ScenarioParameters& parameters) {
    const auto partner = parameters.find(PartnerParameter);
    const auto enemies = parameters.find(EnemiesParameter);
    if (partner == parameters.end() || enemies == parameters.end()) {
      return core::fail("battle scenario: needs --param partner=<species> and --param "
                        "enemies=<species>[,<species>...]");
    }
    auto given = numberParameter<std::int32_t>(parameters, StatsParameter);
    auto hp = numberParameter<std::int32_t>(parameters, HpParameter);
    auto mp = numberParameter<std::int32_t>(parameters, MpParameter);
    auto discipline = numberParameter<std::int32_t>(parameters, DisciplineParameter);
    auto frameLimit = numberParameter<std::int64_t>(parameters, FrameLimitParameter);
    for (const auto* number : {&given, &hp, &mp, &discipline}) {
      if (!*number) {
        return std::unexpected{number->error()};
      }
    }
    if (!frameLimit) {
      return std::unexpected{frameLimit.error()};
    }
    stats_.stats = *given;
    stats_.hp = *hp;
    stats_.mp = mp->value_or(OutOfReachMp);
    discipline_ = discipline->value_or(DefaultDiscipline);
    frameLimit_ = frameLimit->value_or(battle::DefaultFrameLimit);
    partnerId_ = partner->second;
    enemyIds_ = splitList(enemies->second);
    return {};
  }

  // The partner and the enemies first, as the setup lists them.
  [[nodiscard]] std::vector<std::string> fighterIds() const {
    std::vector<std::string> ids{partnerId_};
    ids.insert(ids.end(), enemyIds_.begin(), enemyIds_.end());
    return ids;
  }

  core::Result<void> buildFighters() {
    auto partnerFighter = fighter(partnerId_, stats_, {.x = 0, .z = 0});
    if (!partnerFighter) {
      return std::unexpected{partnerFighter.error()};
    }
    setup_.partner = std::move(*partnerFighter);
    setup_.condition.discipline = discipline_;
    for (std::size_t index = 0; index < enemyIds_.size(); ++index) {
      const auto offset =
          static_cast<std::int32_t>(index) - static_cast<std::int32_t>(enemyIds_.size() / 2);
      auto enemy =
          fighter(enemyIds_[index], stats_, {.x = offset * EnemySpacing, .z = EnemyDistance});
      if (!enemy) {
        return std::unexpected{enemy.error()};
      }
      setup_.enemies.push_back(std::move(*enemy));
    }
    // Battle::start checks the rest (how many enemies, the moves' slots) with its own messages.
    if (auto check =
            battle::Battle::start(setup_, rules_, *moves_, battle::originalStrategies(), 0);
        !check) {
      return std::unexpected{check.error()};
    }
    return {};
  }

  // A fighter of a species with the first techniques of its list.
  [[nodiscard]] core::Result<battle::Combatant>
  fighter(std::string_view id, const GivenStats& stats, battle::Point at) const {
    const auto species = std::ranges::find(digimon_, id, &assets::DigimonRow::id);
    if (species == digimon_.end()) {
      return core::fail("battle scenario: no species '" + std::string{id} +
                        "' in the digimon table");
    }
    battle::Combatant combatant = battle::combatantOf(*species);
    const std::int32_t value = stats.stats.value_or(StatsPerLevel * std::max(species->level, 1));
    combatant.stats.maxHp = stats.hp.value_or(value * HpPerStat);
    combatant.stats.maxMp = stats.mp;
    combatant.stats.offense = value;
    combatant.stats.defense = value;
    combatant.stats.speed = value;
    combatant.stats.brain = value;
    combatant.hp = combatant.stats.maxHp;
    combatant.mp = combatant.stats.maxMp;
    std::size_t slot = 0;
    for (const std::optional<std::string>& move : combatant.speciesMoves) {
      if (!move || slot == battle::FinisherSlot) {
        continue;
      }
      const auto ref = moves_->find(*move);
      if (ref && !moves_->at(*ref).finisher && !moves_->at(*ref).counter) {
        combatant.moves.at(slot) = *move;
        combatant.movePriorities.at(slot) = MovePriority;
        ++slot;
      }
    }
    combatant.position = at;
    return combatant;
  }

  battle::BattleRules rules_;
  std::optional<ruleset::Section> section_;
  std::vector<assets::DigimonRow> digimon_;
  std::unique_ptr<battle::MoveCatalog> moves_;
  std::string partnerId_;
  std::vector<std::string> enemyIds_;
  GivenStats stats_;
  std::int32_t discipline_ = DefaultDiscipline;
  battle::BattleSetup setup_;
  std::int64_t frameLimit_ = battle::DefaultFrameLimit;
};

const ScenarioRegistration registration{{
    .name = "battle",
    .description =
        "Plays a partner against one to three enemies of the Digimon table and counts how the battles end.",
    .create = [] { return std::make_unique<BattleScenario>(); },
}};

} // namespace
} // namespace dwe::sim
