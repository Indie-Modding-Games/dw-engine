#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/randomizer/run_settings.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::game::randomizer {

inline constexpr std::string_view RandomizerSection = "randomizer";

// What a ruleset lets the randomizer change (rules/<id>/randomizer.yaml).
struct RandomizerRules {
  // The map NPC slots of plain wild enemies, by map id; every other NPC keeps its species.
  std::map<std::string, std::vector<std::size_t>, std::less<>> enemies;
  // Items that keep every place they are found in and never appear anywhere else.
  std::vector<std::string> keptItems;
  // Items the randomizer never places or drops.
  std::vector<std::string> excludedItems;
  // Species the randomizer never picks.
  std::vector<std::string> excludedSpecies;

  friend bool operator==(const RandomizerRules&, const RandomizerRules&) = default;
};

[[nodiscard]] core::Result<RandomizerRules> readRandomizerRules(const ruleset::Section& section);
[[nodiscard]] ruleset::SectionChecks randomizerSectionChecks();

// The candidates of one pick, sorted so every platform picks the same.
using Pool = std::span<const std::string>;

// The run's picks. Each one hashes the seed with what it is asked about, never the game's own
// random numbers, so the same seed and settings give the same run and an inactive run rolls
// exactly what the original does. A pick gives nothing when the original stays.
class Randomizer {
public:
  Randomizer() = default;
  Randomizer(RunSettings settings, RandomizerRules rules)
      : settings_(settings), rules_(std::move(rules)) {}

  void setSettings(const RunSettings& settings) { settings_ = settings; }
  void setRules(RandomizerRules rules) { rules_ = std::move(rules); }
  [[nodiscard]] const RunSettings& settings() const { return settings_; }
  [[nodiscard]] const RandomizerRules& rules() const { return rules_; }

  // The form a natural evolution from `from` into `target` gives instead, from the target's stage.
  [[nodiscard]] std::optional<std::string> evolution(std::string_view from, std::string_view target,
                                                     Pool stage) const;
  [[nodiscard]] std::optional<std::string> starter(std::string_view original, Pool rookies) const;
  // The chance in 100 of learning each technique, or nothing for the move table's.
  [[nodiscard]] std::optional<std::int32_t> learnChance() const;
  // The species of a map's NPC slot, from its original's stage: only the rules' wild enemies.
  [[nodiscard]] std::optional<std::string> enemy(std::string_view map, std::size_t slot,
                                                 Pool stage) const;
  [[nodiscard]] bool wildEnemy(std::string_view map, std::size_t slot) const;
  // The item lying on a map's cell or in its chest (`spot`) instead of the script's.
  [[nodiscard]] std::optional<std::string> fieldItem(std::string_view map, std::string_view spot,
                                                     std::string_view original, Pool items) const;
  // The item a beaten species drops instead of its table's.
  [[nodiscard]] std::optional<std::string> drop(std::string_view species, std::string_view original,
                                                Pool items) const;

  [[nodiscard]] bool keepsItem(std::string_view item) const;
  [[nodiscard]] bool excludesItem(std::string_view item) const;
  [[nodiscard]] bool excludesSpecies(std::string_view species) const;

  // A number from the seed and the keys alone, the same on every platform.
  [[nodiscard]] std::uint64_t hash(std::string_view domain,
                                   std::initializer_list<std::string_view> keys) const;

private:
  [[nodiscard]] std::optional<std::string>
  pick(std::string_view domain, std::initializer_list<std::string_view> keys, Pool pool) const;

  RunSettings settings_;
  RandomizerRules rules_;
};

} // namespace dwe::game::randomizer
