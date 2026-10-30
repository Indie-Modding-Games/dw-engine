#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/battle/combatant.hpp>
#include <dwe/game/battle/messages.hpp>

#include <algorithm>
#include <cstddef>
#include <utility>

namespace dwe::game::battle {

namespace {

constexpr std::int32_t HighestChargeMode = static_cast<std::int32_t>(ChargeMode::Cautious);
constexpr std::int32_t HighestType = static_cast<std::int32_t>(DigimonType::Virus);

} // namespace

ChargeMode chargeModeOf(std::int32_t stored) {
  return static_cast<ChargeMode>(std::clamp(stored, 0, HighestChargeMode));
}

Combatant combatantOf(const assets::DigimonRow& species) {
  Combatant combatant;
  combatant.species = species.id;
  combatant.level = species.level;
  combatant.type = static_cast<DigimonType>(std::clamp(species.type, 0, HighestType));
  combatant.radius = species.radius;
  combatant.dropItem = species.dropItem;
  combatant.dropChance = species.dropChance;
  for (std::size_t slot = 0; slot < SpeciesSpecials; ++slot) {
    combatant.specials.at(slot) = species.special.at(slot);
  }
  for (std::size_t slot = 0; slot < SpeciesMoves; ++slot) {
    combatant.speciesMoves.at(slot) = species.moves.at(slot);
  }
  return combatant;
}

// overworld.c: loadMapDigimon: the NPC record gives the enemy its stats, its moves as animations,
// its priorities, charge mode, bits and the flag that makes it only target the partner.
core::Result<Combatant> enemyFromMap(const assets::DigimonRow& species, const assets::MapNpc& npc,
                                     Point position) {
  Combatant enemy = combatantOf(species);
  enemy.stats = {.maxHp = npc.hp,
                 .maxMp = npc.mp,
                 .offense = npc.off,
                 .defense = npc.def,
                 .speed = npc.speed,
                 .brain = npc.brain};
  enemy.hp = npc.currentHp > 0 ? std::min(npc.currentHp, npc.hp) : npc.hp;
  enemy.mp = npc.currentMp > 0 ? std::min(npc.currentMp, npc.mp) : npc.mp;
  enemy.chargeMode = chargeModeOf(npc.chargeMode);
  enemy.focusesPartner = npc.unknown2A == 1;
  enemy.bits = npc.bits;
  enemy.position = position;
  if (enemy.stats.maxHp < 1) {
    return std::unexpected{core::Error{messages::formatMessage(messages::NoMaxHp, species.id)}};
  }
  for (std::size_t slot = 0; slot < EquippedSlots; ++slot) {
    enemy.movePriorities.at(slot) = npc.movesPriority.at(slot);
    const std::int32_t animation = npc.moves.at(slot);
    if (animation == EmptyMoveAnimation) {
      continue;
    }
    const std::int32_t index = animation - FirstMoveAnimation;
    // btl.c: loadBattleData empties a slot whose animation names no move of the species
    // (entityGetTechFromAnim gives 0xff), and the enemy fights with the others.
    if (index < 0 || std::cmp_greater_equal(index, SpeciesMoves) ||
        !species.moves.at(static_cast<std::size_t>(index)).has_value()) {
      core::logDebug("[port:battle] {}", messages::formatMessage(messages::NoSpeciesMove,
                                                                 species.id, index, animation));
      continue;
    }
    enemy.moves.at(slot) = species.moves.at(static_cast<std::size_t>(index));
  }
  return enemy;
}

} // namespace dwe::game::battle
