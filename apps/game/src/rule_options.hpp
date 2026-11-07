#pragma once

#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/ruleset/ruleset_store.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace dwe::app {

// The options of the options page that change the game's rules (docs/modulos/apps/game.md,
// «Dificultad» and «Rarezas como opciones»), engine improvements off by default: each one changes
// values of the running game's ruleset, as the tuning panel does, so the systems read them as any
// other rule.

// The difficulty (a UI setting and the options' command with the same name): normal, the
// original's, changes nothing; easy takes the changes of the ruleset's difficulty section
// (rules/<id>/difficulty.yaml), so a mod tunes it.
inline constexpr std::string_view DifficultySetting = "difficulty";
inline constexpr std::string_view DifficultyNormal = "normal";
inline constexpr std::string_view DifficultyEasy = "easy";
inline constexpr std::string_view DifficultySection = "difficulty";
// The original's quirks the player may turn off (true or false; on turns the quirk off).
inline constexpr std::string_view FairSicknessChanceSetting = "fair_sickness_chance";
inline constexpr std::string_view FairEvolutionAverageSetting = "fair_evolution_average";

// A change of a ruleset value: its section, its dotted path ("hunger.window_minutes") and either
// the new value (a YAML text) or a percent of the ruleset's own whole number, rounded down.
struct RuleChange {
  std::string section;
  std::string path;
  std::variant<std::string, std::int32_t> value;
};

// What the player chose of the options that change the rules.
struct RuleChoices {
  std::string difficulty{DifficultyNormal};
  // care.quirks.sickness_chance_wraps off: a negative sickness chance is none, not nearly all.
  bool fairSicknessChance = false;
  // evolution.quirks.selection_average_carries_over off: each candidate's average is its own.
  bool fairEvolutionAverage = false;

  friend bool operator==(const RuleChoices&, const RuleChoices&) = default;
};

// The changes a level of the ruleset's difficulty section makes: none for normal, for a ruleset
// without the section or for a level it does not name; an error naming the section's file when a
// change is not a mapping of section, path and either value or percent.
core::Result<std::vector<RuleChange>> difficultyChanges(const ruleset::Ruleset& rules,
                                                        std::string_view level);

// Every change the choices make on the ruleset: the quirks', then the difficulty's.
core::Result<std::vector<RuleChange>> ruleChanges(const ruleset::Ruleset& rules,
                                                  const RuleChoices& choices);

// Keeps the choices' changes on a running game's ruleset. Each value it changes remembers the
// ruleset's own, and gets it back once no choice changes it any more; a value someone else changed
// in between (the tuning panel) is left alone. A reload or another ruleset drops the changes, so
// they are made again whenever the store's revision moves.
class RuleOptions {
public:
  // The choices from now on; the next keep applies them.
  void choose(RuleChoices choices);
  [[nodiscard]] const RuleChoices& choices() const { return choices_; }

  // Makes the store's ruleset follow the choices. Cheap while nothing changed: call it every frame.
  // A change that cannot be made (a path the ruleset lacks, a value its checks refuse) is logged
  // and skipped.
  void keep(ruleset::RulesetStore& store);

private:
  // A value a choice changed: the ruleset's own and the one set, both as emitted YAML.
  struct Changed {
    std::string own;
    std::string set;
  };
  using ValueKey = std::pair<std::string, std::string>;

  void apply(ruleset::RulesetStore& store, const RuleChange& change);
  void restore(ruleset::RulesetStore& store, const ValueKey& key, const Changed& changed);

  RuleChoices choices_;
  std::map<ValueKey, Changed> changed_;
  std::optional<std::uint64_t> keptRevision_;
};

} // namespace dwe::app
