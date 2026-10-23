#include <dwe/core/log.hpp>
#include <dwe/game/story/npc_orders.hpp>

#include <utility>

namespace dwe::game::story {

namespace {

constexpr std::int64_t AutotalkOn = 1;

bool isCharacter(std::int64_t entity) {
  return entity == PlayerEntity || entity == PartnerEntity;
}

} // namespace

NpcOrders::NpcOrders(NpcPort& npcs, std::function<void()> freeze)
    : npcs_{&npcs}, freeze_{std::move(freeze)} {}

core::Result<void> NpcOrders::loadSpeciesModel(std::int64_t species) {
  core::logDebug("[port:story] load_species_model (script_interp.c:1311, scriptLoadModel :2561): "
                 "species {}",
                 species);
  npcs_->loadSpeciesModel(species);
  return {};
}

core::Result<void> NpcOrders::unloadSpeciesModel(std::int64_t species) {
  core::logDebug("[port:story] unload_species_model (unloadDigimonModel, overworld.c:1151): "
                 "species {}",
                 species);
  npcs_->unloadSpeciesModel(species);
  return {};
}

core::Result<void> NpcOrders::spawnNpc(std::int64_t species, std::int64_t slot,
                                       std::int64_t autotalk) {
  // script_interp.c:1315: scriptPauseGame(0xff) before scriptSetDigimon.
  freeze_();
  // overworld.c: scriptSetDigimon keeps the byte: only 1 touches without confirm (tamer.c:669).
  const bool spawned = npcs_->spawnNpc(species, slot, autotalk == AutotalkOn);
  core::logDebug(
      "[port:story] spawn_npc (script_interp.c:1315): species {} slot {} autotalk {}: {}", species,
      slot, autotalk, spawned ? "spawned" : "not spawned");
  return {};
}

core::Result<void> NpcOrders::unloadEntity(std::int64_t entity) {
  // script_interp.c:1321: scriptPauseGame(0xff) before scriptUnloadEntity.
  freeze_();
  if (isCharacter(entity)) {
    // The original hides the player or the partner too; no script on the disc does it.
    core::logWarning("[port:story] unload_entity (script_interp.c:1321): entity {} is the player "
                     "or the partner; it stays",
                     entity);
    return {};
  }
  npcs_->unloadNpc(entity);
  return {};
}

core::Result<void> NpcOrders::setAutotalk(std::int64_t entity, std::int64_t enabled) {
  // script_interp.c:1998: the player and the partner (entity ids below 2) are skipped.
  if (isCharacter(entity)) {
    core::logDebug("[port:story] set_autotalk (script_interp.c:1998): entity {} skipped", entity);
    return {};
  }
  npcs_->setAutotalk(entity, enabled == AutotalkOn);
  return {};
}

} // namespace dwe::game::story
