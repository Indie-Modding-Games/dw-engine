#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/evolution/tables.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_game_orders.hpp>
#include <dwe/session/story_lua.hpp>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "story_game_context.hpp"
#include "story_game_messages.hpp"

namespace dwe::session {

namespace {

namespace msg = story_game_messages;

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view StatLimitsKey = "stat_limits";
constexpr std::string_view HappinessFloorKey = "happiness_floor";
constexpr std::string_view DateYearLimitKey = "date_year_limit";
constexpr std::string_view DateOverflowKey = "date_overflow";
constexpr std::size_t DateParts = 4;

// The stats' names in stat_limits, by their number; the current HP and MP have none, as they stop
// at their maximum (script_value.c: enforceStatsLimits).
constexpr std::array<std::string_view, ScriptStatCount> StatNames{"offense",
                                                                  "defense",
                                                                  "speed",
                                                                  "brain",
                                                                  "max_hp",
                                                                  "max_mp",
                                                                  "",
                                                                  "",
                                                                  "tiredness",
                                                                  "happiness",
                                                                  "discipline",
                                                                  "energy",
                                                                  "virus",
                                                                  "lifetime",
                                                                  "merit",
                                                                  "started_battles",
                                                                  "fled_battles",
                                                                  "tournaments_won",
                                                                  "tournament_wins",
                                                                  "tournaments_lost",
                                                                  "weight",
                                                                  "tamer_level",
                                                                  "lives"};

// A story variable is one byte in the state (script.h: ScriptState.pstats).
constexpr std::int64_t VariableByteMax = std::numeric_limits<std::uint8_t>::max();

std::array<std::int32_t, ScriptStatCount> readStatLimits(YamlReader& reader, const YamlNode& node) {
  std::array<std::int32_t, ScriptStatCount> limits{};
  if (!reader.expectKeys(node, StatLimitsKey,
                         {"offense",         "defense",         "speed",
                          "brain",           "max_hp",          "max_mp",
                          "tiredness",       "happiness",       "discipline",
                          "energy",          "virus",           "lifetime",
                          "merit",           "started_battles", "fled_battles",
                          "tournaments_won", "tournament_wins", "tournaments_lost",
                          "weight",          "tamer_level",     "lives"})) {
    return limits;
  }
  for (std::size_t stat = 0; stat < ScriptStatCount; ++stat) {
    if (StatNames.at(stat).empty()) {
      continue;
    }
    const YamlNode& value = reader.field(node, StatNames.at(stat));
    limits.at(stat) = reader.integer<std::int32_t>(value);
    if (reader.ok() && limits.at(stat) < 0) {
      reader.fail(value, msg::formatMessage(msg::LimitNegative, StatNames.at(stat)));
    }
  }
  return limits;
}

ruleset::GameTime readDateOverflow(YamlReader& reader, const YamlNode& node) {
  const auto parts = reader.sequence(node, DateOverflowKey, DateParts);
  if (!reader.ok()) {
    return {};
  }
  const ruleset::GameTime time{.year = reader.integer<std::int32_t>(parts[0]),
                               .day = reader.integer<std::int32_t>(parts[1]),
                               .hour = reader.integer<std::int32_t>(parts[2]),
                               .minute = reader.integer<std::int32_t>(parts[3])};
  if (reader.ok() &&
      (time.year < 0 || time.day < 0 || time.hour < 0 || time.hour >= ruleset::HoursPerDay ||
       time.minute < 0 || time.minute >= ruleset::MinutesPerHour)) {
    reader.fail(node, msg::formatMessage(msg::OverflowOutside, DateOverflowKey));
  }
  return time;
}

} // namespace

core::Result<StoryValueRules> readStoryValueRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, msg::StoryValuesText,
                         {StatLimitsKey, HappinessFloorKey, DateYearLimitKey, DateOverflowKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  StoryValueRules rules{
      .statLimits = readStatLimits(reader, reader.field(root, StatLimitsKey)),
      .happinessFloor = reader.integer<std::int32_t>(reader.field(root, HappinessFloorKey)),
      .dateYearLimit = reader.integer<std::int32_t>(reader.field(root, DateYearLimitKey)),
      .dateOverflow = readDateOverflow(reader, reader.field(root, DateOverflowKey))};
  if (reader.ok() && rules.happinessFloor > 0) {
    reader.fail(root, msg::formatMessage(msg::FloorPositive, rules.happinessFloor));
  }
  if (reader.ok() && rules.dateYearLimit < 0) {
    reader.fail(root, msg::formatMessage(msg::LimitNegative, DateYearLimitKey));
  }
  return reader.finish(rules, section.file);
}

GameSession& StoryGameContext::game() const {
  DWE_ASSERT(session != nullptr);
  return *session;
}

core::Result<std::int64_t> StoryGameContext::variable(std::int64_t variable) const {
  if (!std::in_range<std::int32_t>(variable)) {
    return core::fail(msg::formatMessage(msg::NoSuchVariable, variable));
  }
  const auto value = desc.story->state().variable(static_cast<std::int32_t>(variable));
  if (!value) {
    return std::unexpected{value.error()};
  }
  return std::int64_t{*value};
}

core::Result<void> StoryGameContext::setVariable(std::int64_t variable, std::int64_t value) const {
  if (!std::in_range<std::int32_t>(variable)) {
    return core::fail(msg::formatMessage(msg::NoSuchVariable, variable));
  }
  // writePStat stores the low byte (script_interp.c:2775); the orders' values fit it but a year.
  const auto stored =
      static_cast<std::uint8_t>(std::clamp<std::int64_t>(value, 0, VariableByteMax));
  core::logDebug("[port:story] writePStat (script_interp.c:2775): variable {} = {}", variable,
                 stored);
  return desc.story->state().setVariable(static_cast<std::int32_t>(variable), stored);
}

core::Result<void> StoryGameContext::setTrigger(std::int64_t trigger, bool set) const {
  if (!std::in_range<std::int32_t>(trigger)) {
    return core::fail(msg::formatMessage(msg::NoSuchTrigger, trigger));
  }
  core::logDebug("[port:story] {} (script_interp.c:{}): trigger {}",
                 set ? "setTrigger" : "unsetTrigger", set ? 2430 : 2441, trigger);
  return desc.story->state().setTrigger(static_cast<std::int32_t>(trigger), set);
}

core::Result<void>
publishOrders(script::ApiRegistry& api,
              std::initializer_list<std::pair<std::string_view, script::ApiFunction>> orders) {
  for (const auto& [name, run] : orders) {
    if (auto published = api.publish(StoryNamespace, name, run); !published) {
      return published;
    }
  }
  return {};
}

StoryGameOrders::StoryGameOrders(StoryGameOrdersDesc desc)
    : context_(std::make_unique<StoryGameContext>()) {
  DWE_ASSERT(desc.story != nullptr);
  context_->desc = desc;
}

StoryGameOrders::~StoryGameOrders() = default;

ruleset::SectionChecks StoryGameOrders::sectionChecks() const {
  return {
      {std::string{StoryValuesSection}, [](const ruleset::Section& section) -> core::Result<void> {
         if (auto read = readStoryValueRules(section); !read) {
           return std::unexpected{read.error()};
         }
         return {};
       }}};
}

core::Result<void> StoryGameOrders::readRules(const ruleset::Ruleset& rules) {
  auto section = rules.requireSection(StoryValuesSection);
  if (!section) {
    return std::unexpected{section.error()};
  }
  auto values = readStoryValueRules(**section);
  if (!values) {
    return std::unexpected{values.error()};
  }
  context_->rules = *values;
  context_->digimon.reset();
  if (const assets::AssetId* table = rules.table(game::evolution::DigimonTableRole);
      table != nullptr && context_->desc.files != nullptr) {
    if (auto digimon = assets::loadDigimonTable(*context_->desc.files, *table)) {
      context_->digimon = std::move(*digimon);
    }
  }
  return {};
}

core::Result<void> StoryGameOrders::startNewGame(GameSession& session) {
  bind(session);
  return {};
}

void StoryGameOrders::tick(GameSession& session, const ruleset::ClockAdvance& /*advance*/) {
  bind(session);
}

std::vector<StoryMapEdit> StoryGameOrders::takeMapEdits() {
  return std::exchange(context_->mapEdits, {});
}

void StoryGameOrders::bind(GameSession& session) {
  context_->session = &session;
  if (published_) {
    return;
  }
  script::ScriptVm* vm = context_->desc.story->vm();
  if (vm == nullptr) {
    core::logWarning("{}", msg::NoStoryVm);
    return;
  }
  script::ApiRegistry api = vm->api();
  for (auto* publish : {&publishBagOrders, &publishPartnerOrders, &publishDateOrders,
                        &publishMapOrders, &publishFieldItemOrders, &publishCardOrders}) {
    if (auto published = publish(api, *context_); !published) {
      core::logError("{}", published.error().message);
    }
  }
  published_ = true;
  core::logDebug("[port:story] the story's game orders are published");
}

} // namespace dwe::session
