#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>

namespace dwe::game::partner {

// The partner's six stats, in the order the original keeps its training gains (trn_reward.c:
// STATS_GAINS).
enum class Stat : std::uint8_t { Hp, Mp, Offense, Defense, Speed, Brain };
inline constexpr std::size_t StatCount = 6;
inline constexpr std::array<Stat, StatCount> AllStats{Stat::Hp,      Stat::Mp,    Stat::Offense,
                                                      Stat::Defense, Stat::Speed, Stat::Brain};

// A value for each stat.
struct StatBlock {
  std::array<std::int32_t, StatCount> values{};

  [[nodiscard]] std::int32_t& operator[](Stat stat) {
    return values.at(static_cast<std::size_t>(stat));
  }
  [[nodiscard]] std::int32_t operator[](Stat stat) const {
    return values.at(static_cast<std::size_t>(stat));
  }

  friend bool operator==(const StatBlock&, const StatBlock&) = default;
};

// A set of stats, such as the ones a training boost multiplies.
class StatSet {
public:
  StatSet() = default;
  StatSet(std::initializer_list<Stat> stats) {
    for (const Stat stat : stats) {
      add(stat);
    }
  }

  void add(Stat stat) { bits_ |= bitOf(stat); }
  [[nodiscard]] bool contains(Stat stat) const { return (bits_ & bitOf(stat)) != 0; }
  [[nodiscard]] bool empty() const { return bits_ == 0; }

  friend bool operator==(const StatSet&, const StatSet&) = default;

private:
  static std::uint8_t bitOf(Stat stat) {
    return static_cast<std::uint8_t>(1U << static_cast<unsigned>(stat));
  }

  std::uint8_t bits_ = 0;
};

// The stats of the partner entity (entity.h: Stats): the maximums the original calls base stats,
// and the current HP and MP.
struct PartnerStats {
  StatBlock base;
  std::int32_t currentHp = 0;
  std::int32_t currentMp = 0;
  // How it fights while charging up or cooling down in battle (stats.current.chargeMode): 0 keeps
  // its distance, 1 waits, 2 backs away. Nothing outside battle sets it in the original, so a
  // partner keeps the 0 it starts with; a battle reads it when it starts and puts it back when it
  // ends (BTL_initializeCombat, BTL_deinitializeCombat), whatever the commands did meanwhile.
  std::int32_t chargeMode = 0;

  friend bool operator==(const PartnerStats&, const PartnerStats&) = default;
};

// The needs and states the partner shows (params.h: CONDITION_*).
enum class Condition : std::uint8_t { Sleepy, Tired, Hungry, NeedsToilet, Sulking, Injured, Sick };
inline constexpr std::size_t ConditionCount = 7;

class Conditions {
public:
  [[nodiscard]] bool has(Condition condition) const { return (bits_ & bitOf(condition)) != 0; }
  void set(Condition condition) { bits_ |= bitOf(condition); }
  void clear(Condition condition) { bits_ &= static_cast<std::uint8_t>(~bitOf(condition)); }
  void clearAll() { bits_ = 0; }
  [[nodiscard]] bool none() const { return bits_ == 0; }

  // Whether this is the only condition the partner has.
  [[nodiscard]] bool only(Condition condition) const { return bits_ == bitOf(condition); }

  friend bool operator==(const Conditions&, const Conditions&) = default;

private:
  static std::uint8_t bitOf(Condition condition) {
    return static_cast<std::uint8_t>(1U << static_cast<unsigned>(condition));
  }

  std::uint8_t bits_ = 0;
};

// When the partner gets sleepy and wakes up, and how sleep is going today (params.h: PartnerPara).
struct SleepState {
  std::int32_t sleepyHour = 0;
  std::int32_t sleepyMinute = 0;
  std::int32_t wakeHour = 0;
  std::int32_t wakeMinute = 0;
  std::int32_t awakeHours = 0;
  std::int32_t asleepHours = 0;
  // The time awake left today, in cuts of the sleep section's awake_cut_minutes (timeAwakeToday).
  std::int32_t awakeCuts = 0;
  // What tiredness has added towards the next cut (tirednessSleepTimer).
  std::int32_t tirednessCounter = 0;
  // Hours spent sleepy without sleeping since the last sleep; ten of them start the sickness rolls
  // (sicknessCounter).
  std::int32_t sleepyHoursForSickness = 0;
  std::int32_t missedSleepHours = 0;

  friend bool operator==(const SleepState&, const SleepState&) = default;
};

struct HungerState {
  // Minutes to the next meal (foodLevel).
  std::int32_t foodLevel = 0;
  std::int32_t nextHungerHour = 0;
  // Steps of the hunger window left (starvationTimer); -1 before the first window.
  std::int32_t windowSteps = -1;
  std::int32_t energy = 0;
  // Minutes spent with no energy (emptyStomachTimer).
  std::int32_t emptyStomachMinutes = 0;
  // The favourite food was just refused (refusedFavFood).
  bool refusedFavorite = false;
  // Frames spent tired enough for the meal to come sooner (tirednessHungerTimer).
  std::int32_t tiredHungerFrames = 0;

  friend bool operator==(const HungerState&, const HungerState&) = default;
};

struct ToiletState {
  // Steps to the next need (poopLevel).
  std::int32_t poopLevel = 0;
  // Frames the partner holds on before it goes on the ground (poopingTimer); -1 when it does not
  // need to go.
  std::int32_t urgeFrames = -1;
  std::int32_t virusBar = 0;
  // The urge ran out with a field to play it (partner state 7): the pile and its care effects wait
  // for the scene to place it (partner.c: tickWildPoop, sub state 2).
  bool wildPoopDue = false;

  friend bool operator==(const ToiletState&, const ToiletState&) = default;
};

struct HealthState {
  // Sickness rolls made for staying awake (sicknessTries).
  std::int32_t sicknessTries = 0;
  // Hours sick and hours injured (sicknessTimer, injuryTimer).
  std::int32_t sickHours = 0;
  std::int32_t injuredHours = 0;
  std::int32_t timesSick = 0;

  friend bool operator==(const HealthState&, const HealthState&) = default;
};

// A scold right after a refusal or a poop on the ground counts more (partner_impl.c:
// ITEM_SCOLD_FLAG).
enum class ScoldBoost : std::uint8_t { None, Ready, Used };

struct MoodState {
  std::int32_t happiness = 0;
  std::int32_t discipline = 0;
  std::int32_t tiredness = 0;
  std::int32_t subTiredness = 0;
  // Frames on the current map (areaEffectTimer).
  std::int32_t areaFrames = 0;
  ScoldBoost scoldBoost = ScoldBoost::None;

  friend bool operator==(const MoodState&, const MoodState&) = default;
};

// Whether the partner is alive, and how it died.
enum class LifePhase : std::uint8_t { Alive, DiedNaturally, DiedOtherwise };

struct LifeState {
  std::int32_t remainingLifetime = 0;
  // Days lived.
  std::int32_t age = 0;
  std::int32_t lives = 0;
  // The hour after waking in which the partner cannot die nor get worse (setImmortalHour).
  std::optional<std::int32_t> protectedHour;
  LifePhase phase = LifePhase::Alive;

  friend bool operator==(const LifeState&, const LifeState&) = default;
};

// What the evolution rules read (params.h: careMistakes, battles, evoTimer).
struct EvolutionCounters {
  std::int32_t careMistakes = 0;
  std::int32_t battles = 0;
  // Hours since the last change of level (evoTimer).
  std::int32_t hours = 0;
  std::int32_t fishCaught = 0;

  friend bool operator==(const EvolutionCounters&, const EvolutionCounters&) = default;
};

// A food's boost to the training of some stats (trainBoostFlag, trainBoostValue, trainBoostTimer).
struct TrainingBoost {
  StatSet stats;
  // The multiplier, in tenths.
  std::int32_t value = 0;
  std::int32_t frames = 0;

  friend bool operator==(const TrainingBoost&, const TrainingBoost&) = default;
};

struct TrainingState {
  TrainingBoost boost;
  // Sessions of each stat trained as a baby or in-training (upgrade*Counter).
  StatBlock youngSessions;

  friend bool operator==(const TrainingState&, const TrainingState&) = default;
};

// What the partner kept of itself on turning into the virus form, to come back from it: the form,
// which the original keeps in a script variable (PSTAT_SUKAMON_BACKUP_DIGIMON), and the maximum
// stats (sukaBackup*). An empty species means nothing was kept.
struct VirusBackup {
  std::string species;
  StatBlock stats;

  friend bool operator==(const VirusBackup&, const VirusBackup&) = default;
};

// Everything the original keeps about the partner (params.h: PartnerPara, entity.h: PartnerEntity),
// but its position, model and moves, which the field, evolution and battle keep.
struct PartnerState {
  // The Digimon id (digimon.yaml).
  std::string species;
  PartnerStats stats;
  Conditions conditions;
  SleepState sleep;
  HungerState hunger;
  ToiletState toilet;
  HealthState health;
  MoodState mood;
  LifeState life;
  EvolutionCounters evolution;
  TrainingState training;
  std::int32_t weight = 0;
  // The form and stats before the virus bar turned the partner into its virus form.
  VirusBackup virusBackup;

  friend bool operator==(const PartnerState&, const PartnerState&) = default;
};

} // namespace dwe::game::partner
