#include <dwe/core/log.hpp>
#include <dwe/session/game_hooks.hpp>
#include <dwe/session/messages.hpp>

#include <string>
#include <utility>

namespace dwe::session {

namespace {

using script::ApiValue;
using script::HookField;
using script::HookTable;

HookField text(std::string_view name, std::string_view value) {
  return {.name = std::string{name}, .value = ApiValue{std::string{value}}};
}

HookField integer(std::string_view name, std::int64_t value) {
  return {.name = std::string{name}, .value = ApiValue{value}};
}

HookField boolean(std::string_view name, bool value) {
  return {.name = std::string{name}, .value = ApiValue{value}};
}

} // namespace

core::Result<void> GameHooks::bind(script::HookRegistry hooks) {
  for (const GameEventInfo& info : GameEvents) {
    const std::string_view event = info.name;
    if (auto declared = hooks.declare(event); !declared) {
      return declared;
    }
  }
  hooks_.emplace(hooks);
  return {};
}

void GameHooks::beginFrame() {
  if (hooks_) {
    hooks_->beginFrame();
  }
}

void GameHooks::fire(std::string_view event, HookTable table) {
  if (!hooks_) {
    return;
  }
  table.version = GameEventTableVersion;
  if (auto fired = hooks_->fire(event, table); !fired) {
    core::logError(
        "{}", messages::formatMessage(messages::GameEventFailed, event, fired.error().message));
  }
}

void GameHooks::mapEntered(std::string_view map, std::optional<std::string_view> previous) {
  HookTable table{.fields = {text("map", map)}};
  if (previous) {
    table.fields.push_back(text("previous", *previous));
  }
  fire(game_events::MapEntered, std::move(table));
}

void GameHooks::mapLeft(std::string_view map, std::string_view next) {
  fire(game_events::MapLeft, {.fields = {text("map", map), text("next", next)}});
}

void GameHooks::battleStarted(const BattleStartedEvent& event) {
  HookTable table{.fields = {text("kind", event.kind), text("map", event.map),
                             integer("enemies", event.enemies),
                             boolean("scripted", event.scripted)}};
  if (!event.firstEnemy.empty()) {
    table.fields.push_back(text("first_enemy", event.firstEnemy));
  }
  fire(game_events::BattleStarted, std::move(table));
}

void GameHooks::battleEnded(const BattleEndedEvent& event) {
  HookTable table{.fields = {text("kind", event.kind), text("outcome", event.outcome),
                             boolean("won", event.outcome == "victory")}};
  if (!event.recruit.empty()) {
    table.fields.push_back(text("recruit", event.recruit));
  }
  fire(game_events::BattleEnded, std::move(table));
}

void GameHooks::evolved(std::string_view from, std::string_view to) {
  fire(game_events::Evolved, {.fields = {text("from", from), text("to", to)}});
}

void GameHooks::newDay(std::int32_t year, std::int32_t day) {
  fire(game_events::NewDay, {.fields = {integer("year", year), integer("day", day)}});
}

void GameHooks::itemUsed(std::string_view item, std::string_view context) {
  fire(game_events::ItemUsed, {.fields = {text("item", item), text("context", context)}});
}

void GameHooks::newGame(std::string_view partner) {
  fire(game_events::NewGame, {.fields = {text("partner", partner)}});
}

void GameHooks::loaded() {
  fire(game_events::Loaded, {});
}

} // namespace dwe::session
