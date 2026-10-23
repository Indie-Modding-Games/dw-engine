#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/game/field/npcs.hpp>
#include <dwe/game/story/npc_orders.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <string_view>

namespace dwe::session {

// The story API names of the NPC orders (story_lua.hpp: story_api), published by
// StoryLuaHost::publishNpcApi.
namespace story_api {
inline constexpr std::string_view LoadSpeciesModel = "load_species_model";
inline constexpr std::string_view SpawnNpc = "spawn_npc";
inline constexpr std::string_view UnloadEntity = "unload_entity";
inline constexpr std::string_view SetAutotalk = "set_autotalk";
inline constexpr std::string_view UnloadSpeciesModel = "unload_species_model";

inline constexpr std::array NpcPublished{LoadSpeciesModel, SpawnNpc, UnloadEntity, SetAutotalk,
                                         UnloadSpeciesModel};
} // namespace story_api

// The story's NPC orders on the field's NPCs (game/field npcs.hpp). Without a field attached the
// orders do nothing and say so in the log; the app attaches the field it plays.
class StoryNpcs final : public game::story::NpcPort {
public:
  StoryNpcs() = default;

  // The field's NPCs, and the species table their size comes from (read live: the owner may read it
  // again); both must outlive the attachment (nullptr detaches).
  void attach(game::field::FieldNpcs* npcs, const std::optional<assets::DigimonTable>* species) {
    npcs_ = npcs;
    species_ = species;
  }

  void loadSpeciesModel(std::int64_t species) override;
  void unloadSpeciesModel(std::int64_t species) override;
  bool spawnNpc(std::int64_t species, std::int64_t slot, bool autotalk) override;
  bool unloadNpc(std::int64_t scriptId) override;
  bool setAutotalk(std::int64_t scriptId, bool autotalk) override;

private:
  game::field::FieldNpcs* npcs_ = nullptr;
  const std::optional<assets::DigimonTable>* species_ = nullptr;
  // The models the scripts asked for (LOADED_DIGIMON_MODELS); the app loads a model when an NPC
  // of its species appears, so this is only what the log reports.
  std::multiset<std::int64_t> loadedModels_;
};

} // namespace dwe::session
