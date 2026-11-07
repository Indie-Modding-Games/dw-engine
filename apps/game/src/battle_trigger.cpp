#include "battle_trigger.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/battle/combatant.hpp>
#include <dwe/game/evolution/tables.hpp>

#include <glm/vec3.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "messages.hpp"

namespace dwe::app {
namespace {

constexpr char NpcSeparator = ',';

// The battle's positions are the field's, in world axes, on the ground.
game::battle::Point battlePointOf(glm::vec3 world) {
  return {.x = static_cast<std::int32_t>(std::lround(world.x)),
          .z = static_cast<std::int32_t>(std::lround(world.z))};
}

// Where the NPC in a slot stands when the battle starts: its live place on the field, or, when it
// is not on the map, its record's spawn, which is in the original's axes. By slot: NPCs that share
// a script id each fight from where they stand (btl.c: the fighters are their entities).
glm::vec3 fieldPositionOf(const game::field::Field& field, std::size_t slot,
                          const assets::MapNpc& npc) {
  if (const std::optional<glm::vec3> live = field.npcs().positionAt(slot)) {
    return *live;
  }
  return assets::toWorldPosition(npc.position);
}

} // namespace

bool canFight(const assets::MapNpc& npc, const assets::DigimonTable& species) {
  return npc.digimon && species.find(*npc.digimon) != nullptr && npc.hp > 0;
}

core::Result<assets::DigimonTable> speciesTable(const assets::VirtualFileSystem& files,
                                                const ruleset::Ruleset& rules) {
  const assets::AssetId* table = rules.table(game::evolution::DigimonTableRole);
  if (table == nullptr) {
    return core::fail(std::string{messages::NoSpeciesTable});
  }
  return assets::loadDigimonTable(files, *table);
}

core::Result<std::vector<std::size_t>> npcsOf(std::string_view argument) {
  std::vector<std::size_t> npcs;
  while (!argument.empty()) {
    const std::size_t end = std::min(argument.find(NpcSeparator), argument.size());
    const std::string_view number = argument.substr(0, end);
    std::size_t npc = 0;
    const auto [rest, error] = std::from_chars(number.data(), number.data() + number.size(), npc);
    if (error != std::errc{} || rest != number.data() + number.size()) {
      return core::fail(core::formatMessage(messages::BadNpcList, argument));
    }
    npcs.push_back(npc);
    argument.remove_prefix(std::min(end + 1, argument.size()));
  }
  return npcs;
}

core::Result<MapBattle> mapBattleRequest(const game::field::Field& field,
                                         const assets::MapDocument& map,
                                         const assets::DigimonTable& species,
                                         const std::vector<std::size_t>& npcs) {
  std::vector<std::size_t> chosen = npcs;
  if (chosen.empty()) {
    for (std::size_t index = 0; index < map.npcs.size(); ++index) {
      if (canFight(map.npcs[index], species)) {
        chosen.push_back(index);
        break;
      }
    }
    if (chosen.empty()) {
      return core::fail(core::formatMessage(messages::NoMapFighter, field.mapId()));
    }
  }
  MapBattle battle;
  session::BattleRequest& request = battle.request;
  request = {.enemies = {},
             .scripted = false,
             .tamerless = false,
             .map = field.mapId(),
             .tamer = battlePointOf(field.player().position),
             .partner = battlePointOf(field.partner().position),
             .kind = game::battle::BattleKind::Normal,
             .timeLimit = std::nullopt,
             .recruitOnVictory = std::nullopt,
             .matchMusic = std::nullopt};
  for (const std::size_t index : chosen) {
    if (index >= map.npcs.size()) {
      return core::fail(core::formatMessage(messages::NoMapNpc, field.mapId(), index));
    }
    const assets::MapNpc& npc = map.npcs[index];
    const assets::DigimonRow* row = npc.digimon ? species.find(*npc.digimon) : nullptr;
    if (row == nullptr || !canFight(npc, species)) {
      return core::fail(core::formatMessage(messages::NpcNotAFighter, index, field.mapId()));
    }
    auto enemy =
        game::battle::enemyFromMap(*row, npc, battlePointOf(fieldPositionOf(field, index, npc)));
    if (!enemy) {
      return std::unexpected{enemy.error()};
    }
    core::logDebug(
        "[port:battle] btl.c: the fighters stand where their entities are: NPC slot {} (script "
        "id {}, {}) at ({}, {})",
        index, npc.scriptId, enemy->species, enemy->position.x, enemy->position.z);
    request.enemies.push_back(std::move(*enemy));
    battle.enemyNpcs.push_back(static_cast<std::int32_t>(index));
  }
  return battle;
}

} // namespace dwe::app
