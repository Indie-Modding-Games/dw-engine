#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::partner {

// The training section (rules/<id>/training.yaml, docs/reference/care.md "Entrenamiento").
inline constexpr std::string_view TrainingSection = "training";

// The aptitude tiers of the species' training_type (raise.yaml): types 0 to 3, then every other.
inline constexpr std::size_t AptitudeTierCount = 5;
inline constexpr std::size_t LevelCount = 5;

// A multiplier as a fraction, as the original multiplies its numerator and denominator apart.
struct Ratio {
  std::int32_t multiplier = 1;
  std::int32_t divisor = 1;
};

// One stat a course raises: its base gain and the stat whose food boost multiplies it.
struct CourseGain {
  Stat stat = Stat::Hp;
  std::int32_t base = 0;
  Stat boostedBy = Stat::Hp;
};

// A training course: what it gives and what it costs (trn_reward.c: TRN_award*TrainingGains).
struct TrainingCourse {
  std::string id;
  // The stat whose aptitude, practice and gym upgrade the multiplier uses.
  Stat aptitude = Stat::Hp;
  // Whether the gym upgrade and the practice sessions count (the original's variant 0).
  bool practiceBonuses = false;
  std::int32_t tiredness = 0;
  std::int32_t energy = 0;
  std::int32_t happiness = 0;
  std::vector<CourseGain> gains;
};

// A training place, with the way its hour passes (TRN_advanceTrainingTime,
// TRN2_advanceTrainingTime).
struct TrainingPlace {
  std::string id;
  std::int32_t hungerWindowMinutes = 0;
  std::int32_t foodMinutes = 0;
  std::int32_t poopSteps = 0;
  std::int32_t urgeFrames = 0;
  // Whether a session as a baby or in-training counts for the practice bonus.
  bool countsPractice = false;
  std::vector<TrainingCourse> courses;

  // The course with this id, or nullptr.
  [[nodiscard]] const TrainingCourse* findCourse(std::string_view course) const;
};

struct TrainingQuirks {
  bool brainTrainingUsesMpAptitude = true;
};

struct TrainingRules {
  std::array<std::array<std::int32_t, AptitudeTierCount>, StatCount> aptitudeTenths{};
  std::array<std::int32_t, LevelCount> levelTenths{};
  Ratio upgradeBonus;
  StatBlock upgradeFlags;
  Ratio practiceBonus;
  std::int32_t practiceSessions = 0;
  std::int32_t practiceLevelBelow = 0;
  Ratio manualBonus;
  std::int32_t tiredOutTiredness = 0;
  std::int32_t tiredOutGain = 0;
  std::int32_t slotDefaultTenths = 0;
  std::int32_t overworkTiredness = 0;
  std::int32_t overworkHappiness = 0;
  std::int32_t overworkDiscipline = 0;
  std::string gainFormula;
  std::vector<TrainingPlace> places;
  TrainingQuirks quirks;

  // The place with this id, or nullptr.
  [[nodiscard]] const TrainingPlace* findPlace(std::string_view place) const;

  // The stat whose multiplier a course uses, after the quirks.
  [[nodiscard]] Stat multiplierStat(const TrainingCourse& course) const;
};

// Reads the training section; errors name the file, line and column.
core::Result<TrainingRules> readTrainingRules(const ruleset::Section& section);

// The name of a stat in the YAML (hp, mp, offense, defense, speed, brain).
std::string_view statName(Stat stat);

} // namespace dwe::game::partner
