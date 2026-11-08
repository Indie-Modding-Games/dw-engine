#pragma once

#include <dwe/core/error.hpp>
#include <dwe/script/hook_registry.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

// The game events that mods' scripts handle with hooks.on (docs/modding/api-lua.md, «Eventos del
// juego»). Each handler gets one table, versioned as script::HookTable says.
namespace dwe::session {

namespace game_events {
// {map, previous}: the player stands on a map; previous is nil for the first map of a play.
inline constexpr std::string_view MapEntered = "field.map_entered";
// {map, next}: the player leaves a map for another.
inline constexpr std::string_view MapLeft = "field.map_left";
// {kind, map, enemies, first_enemy, scripted}: a battle starts. kind is "normal", "tournament" or
// "arena"; enemies counts them and first_enemy is the first one's species id.
inline constexpr std::string_view BattleStarted = "battle.started";
// {kind, outcome, won, recruit}: a battle ended and its rewards were given; outcome is "victory",
// "fled" or "defeat", and recruit the resident that joins the city after it, or nil.
inline constexpr std::string_view BattleEnded = "battle.ended";
// {from, to}: the partner evolved from one species id to another.
inline constexpr std::string_view Evolved = "evolution.evolved";
// {year, day}: a new day started (the partner's midnight).
inline constexpr std::string_view NewDay = "clock.new_day";
// {item, context}: the player used an item on the partner; context is "field".
inline constexpr std::string_view ItemUsed = "inventory.item_used";
// {partner}: a new game started with this partner species.
inline constexpr std::string_view NewGame = "game.new_game";
// {}: a save was loaded and the game goes on.
inline constexpr std::string_view Loaded = "game.loaded";
} // namespace game_events

// A field of a game event's table: its name and its Lua type ("string", "integer" or "boolean";
// "?" after it: nil when there is nothing to give).
struct GameEventField {
  std::string_view name;
  std::string_view type;
};

// A game event as the documentation lists it (docs/modding/api-lua.md is generated from these).
struct GameEventInfo {
  std::string_view name;
  std::span<const GameEventField> fields;
};

namespace game_event_fields {
inline constexpr std::array<GameEventField, 2> MapEntered{
    {{.name = "map", .type = "string"}, {.name = "previous", .type = "string?"}}};
inline constexpr std::array<GameEventField, 2> MapLeft{
    {{.name = "map", .type = "string"}, {.name = "next", .type = "string"}}};
inline constexpr std::array<GameEventField, 5> BattleStarted{
    {{.name = "kind", .type = "string"},
     {.name = "map", .type = "string"},
     {.name = "enemies", .type = "integer"},
     {.name = "first_enemy", .type = "string?"},
     {.name = "scripted", .type = "boolean"}}};
inline constexpr std::array<GameEventField, 4> BattleEnded{
    {{.name = "kind", .type = "string"},
     {.name = "outcome", .type = "string"},
     {.name = "won", .type = "boolean"},
     {.name = "recruit", .type = "string?"}}};
inline constexpr std::array<GameEventField, 2> Evolved{
    {{.name = "from", .type = "string"}, {.name = "to", .type = "string"}}};
inline constexpr std::array<GameEventField, 2> NewDay{
    {{.name = "year", .type = "integer"}, {.name = "day", .type = "integer"}}};
inline constexpr std::array<GameEventField, 2> ItemUsed{
    {{.name = "item", .type = "string"}, {.name = "context", .type = "string"}}};
inline constexpr std::array<GameEventField, 1> NewGame{{{.name = "partner", .type = "string"}}};
} // namespace game_event_fields

// Every game event, in the order the documentation lists them.
inline constexpr std::array<GameEventInfo, 9> GameEvents{{
    {.name = game_events::MapEntered, .fields = game_event_fields::MapEntered},
    {.name = game_events::MapLeft, .fields = game_event_fields::MapLeft},
    {.name = game_events::BattleStarted, .fields = game_event_fields::BattleStarted},
    {.name = game_events::BattleEnded, .fields = game_event_fields::BattleEnded},
    {.name = game_events::Evolved, .fields = game_event_fields::Evolved},
    {.name = game_events::NewDay, .fields = game_event_fields::NewDay},
    {.name = game_events::ItemUsed, .fields = game_event_fields::ItemUsed},
    {.name = game_events::NewGame, .fields = game_event_fields::NewGame},
    {.name = game_events::Loaded, .fields = {}},
}};

// The version of every game event's table (script::HookTable::version) in this engine.
inline constexpr std::int64_t GameEventTableVersion = 1;

struct BattleStartedEvent {
  std::string_view kind;
  std::string_view map;
  std::int64_t enemies = 0;
  std::string_view firstEnemy;
  bool scripted = false;
};

struct BattleEndedEvent {
  std::string_view kind;
  std::string_view outcome;
  // Empty when no resident joins.
  std::string_view recruit;
};

// Fires the game events on the game's script VM. Until bound, and when no pack handles an event,
// firing does nothing; a handler's error is the pack's, logged by the registry, and never stops
// the game.
class GameHooks {
public:
  // Declares every game event on the registry and fires them there from now on. The registry's VM
  // must outlive the binding.
  core::Result<void> bind(script::HookRegistry hooks);
  [[nodiscard]] bool bound() const { return hooks_.has_value(); }

  // Gives every pack its hook budget per frame again (once per session tick).
  void beginFrame();

  void mapEntered(std::string_view map, std::optional<std::string_view> previous);
  void mapLeft(std::string_view map, std::string_view next);
  void battleStarted(const BattleStartedEvent& event);
  void battleEnded(const BattleEndedEvent& event);
  void evolved(std::string_view from, std::string_view to);
  void newDay(std::int32_t year, std::int32_t day);
  void itemUsed(std::string_view item, std::string_view context);
  void newGame(std::string_view partner);
  void loaded();

private:
  void fire(std::string_view event, script::HookTable table);

  std::optional<script::HookRegistry> hooks_;
};

} // namespace dwe::session
