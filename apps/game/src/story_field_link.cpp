#include "story_field_link.hpp"

#include <dwe/assets/map_table.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/game/story/screen_history.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_lock.hpp>

#include <algorithm>
#include <string_view>
#include <utility>

#include "field_scene.hpp"

namespace dwe::app {

StoryFieldLink::StoryFieldLink(session::StoryAdapter& story, FieldScene& scene)
    : story_(&story), scene_(&scene) {
  story_->setMapChangeTarget([this](std::int32_t map, std::int32_t exit, bool showName) {
    return playMapChange(map, exit, showName);
  });
  stage_ = std::make_unique<StoryStage>(scene);
  const game::field::FieldRules& rules = scene.field().rules();
  story_->attachStage(stage_.get(), {.frameSeconds = rules.transition.frameSeconds,
                                     .lookTurnPerFrame = rules.scriptMotion.lookTurnSpeed *
                                                         rules.transition.frameSeconds});
  story_->setFieldHoldTarget([this](bool held) {
    if (held) {
      stage_->freeze();
    } else {
      stage_->release();
    }
  });
  scene.field().setExitFilter([this](std::string_view from, std::string_view to) {
    return story_->lock().exitOpen(from, to);
  });
}

StoryFieldLink::~StoryFieldLink() {
  scene_->field().setExitFilter({});
  story_->setMapChangeTarget({});
  story_->setFieldHoldTarget({});
  story_->attachStage(nullptr, {});
}

void StoryFieldLink::afterFieldTick() {
  if (const auto closed = scene_->field().takeClosedExit()) {
    core::logInfo("[port:story-lock] stage {} keeps the exit of {} (cell {}) to {} exit {} closed",
                  story_->lock().stage(), scene_->field().mapId(), closed->trigger, closed->map,
                  closed->exit);
    noticeLock();
  }
  story_->runCellSections(openCellSections());
}

std::vector<std::int32_t> StoryFieldLink::openCellSections() {
  const session::StoryLock& lock = story_->lock();
  const std::string& map = scene_->field().mapId();
  std::vector<std::int32_t> open;
  std::vector<std::int32_t> closed;
  if (stalledCell_) {
    if (assets::collisionCellAt(scene_->field().player().position) == *stalledCell_) {
      return open;
    }
    stalledCell_.reset();
  }
  for (const std::int32_t section : scene_->field().cellSections()) {
    if (lock.sectionOpen(map, section)) {
      open.push_back(section);
      continue;
    }
    closed.push_back(section);
    if (!std::ranges::contains(closedSections_, section)) {
      core::logInfo("[port:story-lock] stage {} keeps section {} of {} closed", lock.stage(),
                    section, map);
      noticeLock();
    }
  }
  closedSections_ = std::move(closed);
  return open;
}

void StoryFieldLink::noticeLock() {
  lockNoticed_ = true;
}

std::optional<std::string> StoryFieldLink::takeLockNotice() {
  if (!std::exchange(lockNoticed_, false)) {
    return std::nullopt;
  }
  return std::string{story_->lock().closedMessage()};
}

void StoryFieldLink::afterMapChange() {
  if (scene_->mapSerial() != mapSerial_) {
    mapSerial_ = scene_->mapSerial();
    const assets::MapTable* table = scene_->mapTable();
    const assets::MapEntry* entry =
        table != nullptr ? table->find(scene_->field().mapId()) : nullptr;
    mapNumber_.reset();
    closedSections_.clear();
    stalledCell_.reset();
    if (!story_->lock().mapOpen(scene_->field().mapId())) {
      core::logWarning("[port:story-lock] the player is on {}, beyond stage {}: they stay, and "
                       "only exits into open maps work",
                       scene_->field().mapId(), story_->lock().stage());
    }
    if (entry != nullptr && entry->originalIndex) {
      mapNumber_ = entry->index;
      followScreen(entry->index);
      story_->mapEntered(entry->index);
    } else {
      core::logDebug("[port:story] changeMap (map.c: changeMap): map {} has no number of the "
                     "original, so no map-head section",
                     scene_->field().mapId());
    }
  }
  scene_->setStoryCells(mapNumber_.has_value() && story_->scriptsMap(*mapNumber_));
}

// map.c: changeMap: the screen left becomes the previous one; tamer.c: checkMapInteraction keeps
// a walked warp's number as the previous exit, map.c: scriptTickChangeMap the exit left by.
void StoryFieldLink::followScreen(std::int32_t map) {
  if (screens_ == nullptr) {
    return;
  }
  const FieldScene::MapArrival& arrival = scene_->mapArrival();
  screens_->arrive({.screen = map,
                    .exit = arrival.exit,
                    .walkedWarp = scene_->field().rules().triggers.exitOf(arrival.trigger)});
}

bool StoryFieldLink::playMapChange(std::int32_t map, std::int32_t exit, bool showName) {
  const assets::MapTable* table = scene_->mapTable();
  const assets::MapEntry* entry = table != nullptr ? table->findByOriginalIndex(map) : nullptr;
  if (entry == nullptr || !entry->hasFile()) {
    core::logError("[port:story] scriptTickChangeMap (map.c:5277): no map {} to change to", map);
    scene_->field().releaseShade();
    return false;
  }
  if (!story_->lock().exitOpen(scene_->field().mapId(), entry->id)) {
    core::logInfo("[port:story-lock] scriptTickChangeMap (map.c:5277): stage {} keeps {} closed; "
                  "the player stays on {}",
                  story_->lock().stage(), entry->id, scene_->field().mapId());
    stalledCell_ = assets::collisionCellAt(scene_->field().player().position);
    noticeLock();
    // A fled battle's way back held the screen black for this change: it comes back.
    scene_->field().releaseShade();
    return false;
  }
  scene_->startScriptedMapChange({.map = entry->id, .exit = exit, .trigger = 0}, showName);
  return true;
}

} // namespace dwe::app
