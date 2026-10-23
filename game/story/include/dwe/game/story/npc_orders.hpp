#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <functional>

// The story orders that put the map's NPCs on the field and take them off (script_interp.c:
// scriptInstruction46to58 0x46-0x48, scriptInstruction64to7E 0x6A and 0x79; overworld.c:
// scriptSetDigimon, scriptUnloadEntity).
namespace dwe::game::story {

// The script ids of the player and the partner in an entity operand (script_value.c:
// scriptIdToEntityId); any other is an NPC's script id.
inline constexpr std::int64_t PlayerEntity = 0xFD;
inline constexpr std::int64_t PartnerEntity = 0xFC;
// The owners of a pause from here up are no script id (script_interp.c: scriptPauseGame).
inline constexpr std::int64_t FirstSystemOwner = 0xC8;

// The field's NPCs, as the story sees them. The session implements it over game/field.
class NpcPort {
public:
  NpcPort() = default;
  NpcPort(const NpcPort&) = delete;
  NpcPort& operator=(const NpcPort&) = delete;
  NpcPort(NpcPort&&) = delete;
  NpcPort& operator=(NpcPort&&) = delete;
  virtual ~NpcPort() = default;

  // The species' model is needed on this map, or no longer (scriptLoadModel, unloadDigimonModel).
  virtual void loadSpeciesModel(std::int64_t species) = 0;
  virtual void unloadSpeciesModel(std::int64_t species) = 0;
  // Puts the NPC of the map's record `slot` on the map if the record is of `species`; whether it
  // did (scriptSetDigimon).
  virtual bool spawnNpc(std::int64_t species, std::int64_t slot, bool autotalk) = 0;
  // Takes the NPC with this script id off the map; whether there was one.
  virtual bool unloadNpc(std::int64_t scriptId) = 0;
  // Whether touching the NPC runs its section; whether there was one.
  virtual bool setAutotalk(std::int64_t scriptId, bool autotalk) = 0;
  // Holds the NPC still for the script or lets it go; whether there was one (setMovementEnabled).
  virtual bool holdNpc(std::int64_t scriptId, bool held) = 0;
  // The same for every NPC on the map.
  virtual void holdAllNpcs(bool held) = 0;
};

class NpcOrders {
public:
  // `freeze` holds the player still for the script (StoryRunner::freezeWorld); the port and
  // whatever `freeze` points to must outlive the orders.
  NpcOrders(NpcPort& npcs, std::function<void()> freeze);

  // load_species_model and unload_species_model.
  core::Result<void> loadSpeciesModel(std::int64_t species);
  core::Result<void> unloadSpeciesModel(std::int64_t species);
  // spawn_npc: freezes the world, then puts the record's NPC on the map; a record of another
  // species puts nothing, as the original.
  core::Result<void> spawnNpc(std::int64_t species, std::int64_t slot, std::int64_t autotalk);
  // unload_entity: freezes the world, then takes the NPC off the map.
  core::Result<void> unloadEntity(std::int64_t entity);
  // set_autotalk: the player and the partner have none.
  core::Result<void> setAutotalk(std::int64_t entity, std::int64_t enabled);

  // The NPC side of a pause (script_interp.c: scriptPauseGame): an owner below FirstSystemOwner is
  // an NPC's script id, and that NPC is held still (the speaker of a textbox, script_common.c).
  void holdOwner(std::int64_t owner);
  // The script ended while the world was frozen: every NPC moves again (script_interp.c:
  // tickScript, setMovementEnabled(-1, 0)).
  void releaseAll();
  // Trigger 1 holds every NPC (script_interp.c:1944, setMovementEnabled(-1, 1)).
  void holdAll();

private:
  NpcPort* npcs_;
  std::function<void()> freeze_;
};

} // namespace dwe::game::story
