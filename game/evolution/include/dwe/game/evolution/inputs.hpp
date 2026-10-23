#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace dwe::game::evolution {

// A Digimon type: its index in the Digimon table (the original's DIGIMON_DATA), as the evolution
// tables use it.
using Species = std::int32_t;

// The levels of the Digimon table's `level` field.
enum class Level : std::uint8_t {
  None = 0,
  Baby = 1,
  InTraining = 2,
  Rookie = 3,
  Champion = 4,
  Ultimate = 5,
};

// The partner's base stats (the original's BaseStats).
struct BaseStats {
  std::int32_t hp = 0;
  std::int32_t mp = 0;
  std::int32_t offense = 0;
  std::int32_t defense = 0;
  std::int32_t speed = 0;
  std::int32_t brains = 0;

  friend bool operator==(const BaseStats&, const BaseStats&) = default;
};

// What was kept of the partner on turning into the virus form, to bring it back: the form, which
// the original keeps in a script variable (PSTAT_SUKAMON_BACKUP_DIGIMON), and the base stats, which
// the partner keeps (sukaBackup*).
struct VirusBackup {
  Species species = 0;
  BaseStats stats{};

  friend bool operator==(const VirusBackup&, const VirusBackup&) = default;
};

// What the evolution rules read of the partner and of the game. The session fills it from the
// partner's state; the evolution module never sees the partner itself.
struct EvolutionInputs {
  Species species = 0;
  BaseStats stats{};
  std::int32_t weight = 0;
  std::int32_t careMistakes = 0;
  std::int32_t discipline = 0;
  std::int32_t happiness = 0;
  std::int32_t tiredness = 0;
  std::int32_t battles = 0;
  // How many moves the partner has learned (evolution.c: getNumMasteredMoves counts the bits of its
  // 64-bit mask).
  std::int32_t learnedMoves = 0;
  // Days lived. The original's rules do not read it; it is here for other rulesets' strategies.
  std::int32_t ageDays = 0;
  // Hours of the evolution counter (evoTimer) since the last change of level.
  std::int32_t evolutionTimer = 0;
  // The id in data/maps of the map the partner is on (CURRENT_SCREEN), or empty when it is on none.
  // NOLINTNEXTLINE(readability-redundant-member-init): the default lets designated initializers leave it out.
  std::string map = {};
  // Whether the tamer is free (tamerGetState() == 0): the natural and special evolutions wait for
  // it.
  bool tamerFree = true;
  // The original's IS_SCRIPT_PAUSED flag, which gates the tamer level chance on evolving. What it
  // stands for at that moment is a supposition (docs/reference/evolution.md).
  bool scriptPaused = false;
  // The form a full virus bar turns the partner into, which the care section owns
  // (care.toilet.virus_form); none when the ruleset names none.
  std::optional<Species> virusForm = std::nullopt;
  // What was kept on turning into the virus form, if anything: the way back and its stats rebuild
  // read it.
  std::optional<VirusBackup> virusBackup = std::nullopt;
};

// The stretch of game time a natural check covers. partner_impl.c: tickConditions checks once per
// frame of the game clock (CURRENT_FRAME), so the check runs once for each frame the clock went
// on, at the counter value of that frame.
struct CheckSpan {
  // Frames the clock went on in the stretch; with 1 the check runs once, at the counter now.
  std::int64_t frames = 1;
  // Frames of the current hour that had already gone at the end of the stretch: 0 when the hour
  // began on its last frame.
  std::int64_t frameInHour = 0;
  // Frames per game hour (clock: frames_per_minute * 60).
  std::int64_t framesPerHour = 1;

  // One check for each counter value that `hoursCrossed` hours went through, and one at the
  // counter now when no hour was crossed.
  [[nodiscard]] static CheckSpan hourly(std::int64_t hoursCrossed) {
    return {.frames = hoursCrossed > 1 ? hoursCrossed : 1, .frameInHour = 0, .framesPerHour = 1};
  }
};

// A praise or a scold, for the rules that run after it: the discipline and happiness the partner
// had before it.
struct PraiseScold {
  bool scolded = false;
  std::int32_t disciplineBefore = 0;
  std::int32_t happinessBefore = 0;
};

} // namespace dwe::game::evolution
