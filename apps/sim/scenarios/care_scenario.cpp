// The care scenario: raises a partner for --days days at --speed with a simple scripted carer, and
// reports how the raisings went: lifespan, care mistakes, sicknesses, deaths, final stats and
// weight (docs/guides/simulaciones.md). Once every game minute the carer feeds a hungry partner,
// takes it to the toilet, puts it to bed when it is sleepy or trains it at its hour of the day;
// --param changes each habit. There is no evolution system yet: the partner keeps its species until
// it dies, and is then reborn as a baby.

#include <dwe/game/partner/partner.hpp>
#include <dwe/game/partner/rules.hpp>
#include <dwe/game/partner/species.hpp>
#include <dwe/game/partner/training_rules.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/sim/messages.hpp>
#include <dwe/sim/scenario.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::sim {

namespace {

namespace partner = game::partner;

constexpr std::string_view ScenarioName = "care";

// The parameters (--param key=value) and their defaults.
constexpr std::string_view SpeciesParameter = "species";
constexpr std::string_view FeedParameter = "feed";
constexpr std::string_view ToiletParameter = "toilet";
constexpr std::string_view SleepParameter = "sleep";
constexpr std::string_view TrainSessionsParameter = "train_sessions";
constexpr std::string_view TrainHourParameter = "train_hour";
constexpr std::string_view TrainPlaceParameter = "train_place";
constexpr std::string_view TrainCourseParameter = "train_course";
constexpr std::string_view TamerLevelParameter = "tamer_level";
constexpr std::array<std::string_view, 9> KnownParameters{
    SpeciesParameter,    FeedParameter,          ToiletParameter,
    SleepParameter,      TrainSessionsParameter, TrainHourParameter,
    TrainPlaceParameter, TrainCourseParameter,   TamerLevelParameter};
constexpr std::string_view SwitchOn = "true";
constexpr std::string_view SwitchOff = "false";

// The original's first partner.
constexpr std::string_view DefaultSpecies = "agum";
constexpr std::int32_t DefaultTrainSessions = 2;
constexpr std::int32_t MaxTrainSessions = 23;
constexpr std::int32_t DefaultTrainHour = 10;
constexpr std::int32_t LastHour = ruleset::HoursPerDay - 1;
constexpr std::string_view DefaultTrainPlace = "trn";
constexpr std::string_view DefaultTrainCourse = "hp";
constexpr std::int32_t DefaultTamerLevel = 1;
// The tamer's level stays within these; the session keeps it there (game/partner, "Eventos").
constexpr std::int32_t TamerLevelMin = 0;
constexpr std::int32_t TamerLevelMax = 10;

// The meal the carer gives: the original's meat (item.c: handleFood), energy 12 and weight +1.
constexpr std::string_view MealItem = "meat";
constexpr std::int32_t MealEnergy = 12;
constexpr std::int32_t MealWeight = 1;

// The stats the partner starts with: stand-ins, the session gives the real ones.
constexpr std::array<std::int32_t, partner::StatCount> StartStats{100, 100, 10, 10, 10, 10};

// The carer looks at the partner once every game minute, at any speed.
constexpr std::int64_t CarerTurnMinutes = 1;

// The metrics.
constexpr std::string_view LifespanMetric = "lifespan_days";
constexpr std::string_view DeathsMetric = "deaths";
constexpr std::string_view CareMistakesMetric = "care_mistakes";
constexpr std::string_view SicknessesMetric = "sicknesses";
constexpr std::string_view WeightMetric = "weight";
constexpr std::string_view FinalStatPrefix = "final_";

// What the scripted carer does.
struct CarerPolicy {
  std::string species;
  bool feeds = true;
  bool usesToilet = true;
  bool putsToBed = true;
  // Training sessions in a row once a day at this hour; 0 never trains.
  std::int32_t trainSessions = 0;
  std::int32_t trainHour = 0;
  std::string trainPlace;
  std::string trainCourse;
  std::int32_t tamerLevel = 0;
};

// What a raising counted.
struct RaisingTally {
  std::int32_t careMistakes = 0;
  std::int32_t sicknesses = 0;
  std::int32_t deaths = 0;
  // The age of the first partner when it died; it lived the whole run if it did not.
  std::optional<std::int32_t> firstLifespanDays;
  std::int32_t tamerLevel = 0;
};

std::string knownParameterNames() {
  std::string names;
  for (const std::string_view name : KnownParameters) {
    if (!names.empty()) {
      names += messages::NameSeparator;
    }
    names += name;
  }
  return names;
}

core::Result<void> checkParameterNames(const ScenarioParameters& parameters) {
  for (const auto& [key, value] : parameters) {
    if (std::ranges::find(KnownParameters, key) == KnownParameters.end()) {
      return core::fail(
          messages::formatMessage(messages::UnknownParameter, key, knownParameterNames()));
    }
  }
  return {};
}

std::string textParameter(const ScenarioParameters& parameters, std::string_view key,
                          std::string_view fallback) {
  const auto found = parameters.find(key);
  return found == parameters.end() ? std::string{fallback} : found->second;
}

core::Result<bool> switchParameter(const ScenarioParameters& parameters, std::string_view key) {
  const auto found = parameters.find(key);
  if (found == parameters.end() || found->second == SwitchOn) {
    return true;
  }
  if (found->second == SwitchOff) {
    return false;
  }
  return core::fail(messages::formatMessage(messages::BadSwitchParameter, key, found->second));
}

core::Result<std::int32_t> wholeParameter(const ScenarioParameters& parameters,
                                          std::string_view key, std::int32_t fallback,
                                          std::int32_t lowest, std::int32_t highest) {
  const auto found = parameters.find(key);
  if (found == parameters.end()) {
    return fallback;
  }
  const std::string& text = found->second;
  std::int32_t value = 0;
  const auto [stop, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || stop != text.data() + text.size() || value < lowest ||
      value > highest) {
    return core::fail(
        messages::formatMessage(messages::BadWholeParameter, key, lowest, highest, text));
  }
  return value;
}

core::Result<CarerPolicy> readPolicy(const ScenarioParameters& parameters) {
  if (auto known = checkParameterNames(parameters); !known) {
    return std::unexpected{known.error()};
  }
  auto feeds = switchParameter(parameters, FeedParameter);
  auto toilet = switchParameter(parameters, ToiletParameter);
  auto sleeps = switchParameter(parameters, SleepParameter);
  auto sessions =
      wholeParameter(parameters, TrainSessionsParameter, DefaultTrainSessions, 0, MaxTrainSessions);
  auto hour = wholeParameter(parameters, TrainHourParameter, DefaultTrainHour, 0, LastHour);
  auto level = wholeParameter(parameters, TamerLevelParameter, DefaultTamerLevel, TamerLevelMin,
                              TamerLevelMax);
  for (const core::Result<bool>* read : {&feeds, &toilet, &sleeps}) {
    if (!*read) {
      return std::unexpected{read->error()};
    }
  }
  for (const core::Result<std::int32_t>* read : {&sessions, &hour, &level}) {
    if (!*read) {
      return std::unexpected{read->error()};
    }
  }
  return CarerPolicy{
      .species = textParameter(parameters, SpeciesParameter, DefaultSpecies),
      .feeds = *feeds,
      .usesToilet = *toilet,
      .putsToBed = *sleeps,
      .trainSessions = *sessions,
      .trainHour = *hour,
      .trainPlace = textParameter(parameters, TrainPlaceParameter, DefaultTrainPlace),
      .trainCourse = textParameter(parameters, TrainCourseParameter, DefaultTrainCourse),
      .tamerLevel = *level};
}

// The real seconds of a carer's turn at a speed above 0: the clock crosses CarerTurnMinutes in
// them.
double turnSeconds(const ruleset::ClockRules& rules, double speed) {
  const auto turnFrames = static_cast<double>(CarerTurnMinutes * rules.framesPerMinute);
  return turnFrames / (rules.framesPerSecond * speed);
}

std::int64_t framesPerDay(const ruleset::ClockRules& rules) {
  return rules.framesPerMinute * ruleset::MinutesPerHour * ruleset::HoursPerDay;
}

partner::PartnerStats startStats() {
  partner::PartnerStats stats;
  stats.base.values = StartStats;
  stats.currentHp = stats.base[partner::Stat::Hp];
  stats.currentMp = stats.base[partner::Stat::Mp];
  return stats;
}

partner::FoodEffect meal() {
  partner::FoodEffect food;
  food.item = MealItem;
  food.energy = MealEnergy;
  food.weight = MealWeight;
  return food;
}

void countEvents(RaisingTally& tally, const std::vector<partner::PartnerEvent>& events,
                 const partner::Partner& raised) {
  for (const partner::PartnerEvent& event : events) {
    switch (event.kind) {
    case partner::EventKind::CareMistake:
      ++tally.careMistakes;
      break;
    case partner::EventKind::GotSick:
      ++tally.sicknesses;
      break;
    case partner::EventKind::Died:
      ++tally.deaths;
      if (!tally.firstLifespanDays) {
        tally.firstLifespanDays = raised.state().life.age;
      }
      break;
    case partner::EventKind::TamerLevelChanged:
      tally.tamerLevel = std::clamp(tally.tamerLevel + event.value, TamerLevelMin, TamerLevelMax);
      break;
    default:
      break;
    }
  }
}

Metrics metricsOf(const partner::Partner& raised, const RaisingTally& tally) {
  const partner::PartnerState& state = raised.state();
  const std::int32_t lifespan = tally.firstLifespanDays.value_or(state.life.age);
  Metrics metrics{
      {.name = std::string{LifespanMetric}, .value = static_cast<double>(lifespan)},
      {.name = std::string{DeathsMetric}, .value = static_cast<double>(tally.deaths)},
      {.name = std::string{CareMistakesMetric}, .value = static_cast<double>(tally.careMistakes)},
      {.name = std::string{SicknessesMetric}, .value = static_cast<double>(tally.sicknesses)},
      {.name = std::string{WeightMetric}, .value = static_cast<double>(state.weight)},
  };
  for (const partner::Stat stat : partner::AllStats) {
    std::string name{FinalStatPrefix};
    name += partner::statName(stat);
    metrics.push_back(
        {.name = std::move(name), .value = static_cast<double>(state.stats.base[stat])});
  }
  return metrics;
}

class CareScenario final : public Scenario {
public:
  core::Result<void> setup(const ScenarioContext& context) override {
    if (context.speed <= 0.0) {
      return core::fail(messages::formatMessage(messages::StoppedClock, ScenarioName));
    }
    auto clock = ruleset::readClockRules(*context.rules);
    if (!clock) {
      return std::unexpected{clock.error()};
    }
    auto rules = partner::loadPartnerRules(*context.rules);
    if (!rules) {
      return std::unexpected{rules.error()};
    }
    auto species = partner::loadSpeciesTable(*context.files, *context.rules);
    if (!species) {
      return std::unexpected{species.error()};
    }
    auto policy = readPolicy(*context.parameters);
    if (!policy) {
      return std::unexpected{policy.error()};
    }
    if (species->find(policy->species) == nullptr) {
      return core::fail(messages::formatMessage(messages::UnknownSpecies, policy->species));
    }
    const partner::TrainingPlace* place = rules->training.findPlace(policy->trainPlace);
    if (policy->trainSessions > 0 &&
        (place == nullptr || place->findCourse(policy->trainCourse) == nullptr)) {
      return core::fail(messages::formatMessage(messages::UnknownTraining, policy->trainCourse,
                                                policy->trainPlace));
    }
    clockRules_ = *clock;
    rules_ = std::move(*rules);
    species_ = std::move(*species);
    policy_ = std::move(*policy);
    speed_ = context.speed;
    days_ = context.days;
    return {};
  }

  [[nodiscard]] core::Result<Metrics> run(std::uint64_t seed) const override {
    ruleset::GameClock clock{clockRules_};
    clock.setSpeed(speed_);
    const double turn = turnSeconds(clockRules_, speed_);
    partner::Partner raised{rules_, species_, seed};
    if (auto started = raised.start(policy_.species, startStats(), clock); !started) {
      return std::unexpected{started.error()};
    }
    RaisingTally tally;
    tally.tamerLevel = policy_.tamerLevel;
    const std::int64_t end = clock.elapsedFrames() + (days_ * framesPerDay(clockRules_));
    std::int64_t lastTrainingDay = -1;
    while (clock.elapsedFrames() < end) {
      raised.tick(clock, clock.advance(turn), {});
      countEvents(tally, raised.takeEvents(), raised);
      if (!raised.alive()) {
        if (auto reborn = raised.rebirth(clock, tally.tamerLevel); !reborn) {
          return std::unexpected{reborn.error()};
        }
      } else if (auto cared = careFor(raised, clock, lastTrainingDay); !cared) {
        return std::unexpected{cared.error()};
      }
      countEvents(tally, raised.takeEvents(), raised);
    }
    return metricsOf(raised, tally);
  }

private:
  // The carer's turn after a step of the loop: one thing at a time, as a player would.
  core::Result<void> careFor(partner::Partner& raised, ruleset::GameClock& clock,
                             std::int64_t& lastTrainingDay) const {
    const partner::Conditions& conditions = raised.state().conditions;
    if (policy_.feeds && conditions.has(partner::Condition::Hungry)) {
      if (!raised.refuses(
              {.item = std::string{MealItem}, .check = partner::RefusalCheck::FavoriteOnly})) {
        raised.eat(meal(), clock);
      }
      return {};
    }
    if (policy_.usesToilet && conditions.has(partner::Condition::NeedsToilet)) {
      raised.useToilet();
      return {};
    }
    if (policy_.putsToBed && raised.canSleep()) {
      raised.sleep(clock, {});
      return {};
    }
    const std::int64_t day = clock.elapsedFrames() / framesPerDay(clockRules_);
    if (policy_.trainSessions == 0 || day == lastTrainingDay ||
        clock.time().hour != policy_.trainHour) {
      return {};
    }
    lastTrainingDay = day;
    for (std::int32_t session = 0; session < policy_.trainSessions && raised.alive(); ++session) {
      if (auto trained =
              raised.train({.place = policy_.trainPlace, .course = policy_.trainCourse}, clock);
          !trained) {
        return std::unexpected{trained.error()};
      }
    }
    return {};
  }

  ruleset::ClockRules clockRules_;
  partner::PartnerRules rules_;
  partner::SpeciesTable species_;
  CarerPolicy policy_;
  double speed_ = 1.0;
  std::int64_t days_ = 1;
};

const ScenarioRegistration registration{{
    .name = std::string{ScenarioName},
    .description =
        "Raises a partner for --days days at --speed with a scripted carer (feeds, toilet, sleep, "
        "training; --param changes each habit) and reports lifespan, care mistakes, sicknesses, deaths, "
        "final stats and weight.",
    .create = [] { return std::make_unique<CareScenario>(); },
}};

} // namespace

} // namespace dwe::sim
