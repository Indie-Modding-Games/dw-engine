#include "story_field_link.hpp"

#include <dwe/assets/map_table.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/session/story_adapter.hpp>

#include "field_scene.hpp"

namespace dwe::app {

StoryFieldLink::StoryFieldLink(session::StoryAdapter& story, FieldScene& scene)
    : story_(&story), scene_(&scene) {
  story_->setMapChangeTarget([this](std::int32_t map, std::int32_t exit, bool showName) {
    return playMapChange(map, exit, showName);
  });
}

StoryFieldLink::~StoryFieldLink() {
  story_->setMapChangeTarget({});
}

void StoryFieldLink::afterFieldTick() {
  story_->runCellSections(scene_->field().cellSections());
}

void StoryFieldLink::afterMapChange() {
  if (scene_->mapSerial() != mapSerial_) {
    mapSerial_ = scene_->mapSerial();
    const assets::MapTable* table = scene_->mapTable();
    const assets::MapEntry* entry =
        table != nullptr ? table->find(scene_->field().mapId()) : nullptr;
    mapNumber_.reset();
    if (entry != nullptr && entry->originalIndex) {
      mapNumber_ = entry->index;
      story_->mapEntered(entry->index);
    } else {
      core::logDebug("[port:story] changeMap (map.c: changeMap): map {} has no number of the "
                     "original, so no map-head section",
                     scene_->field().mapId());
    }
  }
  scene_->setStoryCells(mapNumber_.has_value() && story_->scriptsMap(*mapNumber_));
}

bool StoryFieldLink::playMapChange(std::int32_t map, std::int32_t exit, bool showName) {
  const assets::MapTable* table = scene_->mapTable();
  const assets::MapEntry* entry = table != nullptr ? table->findByOriginalIndex(map) : nullptr;
  if (entry == nullptr || !entry->hasFile()) {
    core::logError("[port:story] scriptTickChangeMap (map.c:5277): no map {} to change to", map);
    return false;
  }
  scene_->startScriptedMapChange({.map = entry->id, .exit = exit, .trigger = 0}, showName);
  return true;
}

} // namespace dwe::app
