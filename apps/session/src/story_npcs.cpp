#include <dwe/assets/content_id.hpp>
#include <dwe/core/log.hpp>
#include <dwe/session/story_npcs.hpp>

#include <cstddef>
#include <format>
#include <limits>
#include <string>

namespace dwe::session {

namespace {

// A species' model is digimon/<species>/model (dwx: digimonSlug), digimon/<pack>/<name>/model for a
// species a pack adds (assets::ContentId::assetPath).
constexpr std::string_view SpeciesModelLeaf = "model";

std::string speciesModel(std::string_view species) {
  return std::format("{}/{}/{}", assets::SpeciesFolder, assets::contentAssetPath(species),
                     SpeciesModelLeaf);
}

} // namespace

void StoryNpcs::loadSpeciesModel(std::int64_t species) {
  loadedModels_.insert(species);
  core::logDebug("[port:npc] scriptLoadModel (script_interp.c:2561): species {}; {} asked for",
                 species, loadedModels_.size());
}

void StoryNpcs::unloadSpeciesModel(std::int64_t species) {
  if (const auto found = loadedModels_.find(species); found != loadedModels_.end()) {
    loadedModels_.erase(found);
  }
  core::logDebug("[port:npc] unloadDigimonModel (overworld.c:1151): species {}; {} left", species,
                 loadedModels_.size());
}

bool StoryNpcs::spawnNpc(std::int64_t species, std::int64_t slot, bool autotalk) {
  if (npcs_ == nullptr) {
    core::logDebug("[port:npc] scriptSetDigimon (overworld.c:1093): no field attached; slot {} "
                   "stays empty",
                   slot);
    return false;
  }
  const auto index = static_cast<std::size_t>(slot);
  const assets::MapNpc* record = slot >= 0 ? npcs_->record(index) : nullptr;
  if (record == nullptr || !record->digimon) {
    core::logError("[port:npc] scriptSetDigimon (overworld.c:1093): the map has no NPC record {} "
                   "with a species id",
                   slot);
    return false;
  }
  const assets::DigimonRow* row =
      species_ != nullptr && species_->has_value() ? (*species_)->find(*record->digimon) : nullptr;
  if (row == nullptr) {
    core::logError("[port:npc] scriptSetDigimon (overworld.c:1093): species {} of record {} is not "
                   "in the species table",
                   *record->digimon, slot);
    return false;
  }
  return npcs_->spawn({.slot = index,
                       .species = static_cast<std::int32_t>(species),
                       .autotalk = autotalk,
                       .model = speciesModel(*record->digimon),
                       .radius = static_cast<float>(row->radius)});
}

bool StoryNpcs::unloadNpc(std::int64_t scriptId) {
  if (npcs_ == nullptr || scriptId < 0 || scriptId > std::numeric_limits<std::int32_t>::max()) {
    return false;
  }
  return npcs_->unload(static_cast<std::int32_t>(scriptId));
}

bool StoryNpcs::setAutotalk(std::int64_t scriptId, bool autotalk) {
  if (npcs_ == nullptr || scriptId < 0 || scriptId > std::numeric_limits<std::int32_t>::max()) {
    return false;
  }
  return npcs_->setAutotalk(static_cast<std::int32_t>(scriptId), autotalk);
}

} // namespace dwe::session
