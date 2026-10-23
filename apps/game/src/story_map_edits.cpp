#include "story_map_edits.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/render/map_layers.hpp>
#include <dwe/session/story_game_orders.hpp>
#include <dwe/session/story_map_edits.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>

#include "field_scene.hpp"

namespace dwe::app {

namespace {

constexpr std::string_view NoMapObject = "the story set the flag of object {}, which map {} lacks";

} // namespace

void FieldScene::setMapObjectFlag(std::int32_t object, std::int32_t flag) {
  render::MapDepthLayers& layers = mapRenderer_->layers();
  if (!mapRenderer_->hasMap() || object < 0 ||
      static_cast<std::size_t>(object) >= layers.instanceCount()) {
    // The original writes past the map's objects (overworld.c:935); here nothing changes.
    core::logWarning("{}", core::formatMessage(NoMapObject, object, field_->mapId()));
    return;
  }
  // The renderer draws only the hidden flag of the original's (map_layers.cpp).
  layers.setHidden(static_cast<std::size_t>(object), flag == assets::MapObjectInstance::HiddenFlag);
}

void FieldScene::blockCells(assets::CollisionCell from, std::int32_t width, std::int32_t height) {
  field_->blockCells(from, width, height);
}

void applyStoryMapEdits(FieldScene& scene, session::StoryGameOrders* orders) {
  if (orders == nullptr) {
    return;
  }
  for (const session::StoryMapEdit& edit : orders->takeMapEdits()) {
    if (const auto* flag = std::get_if<session::StoryObjectFlag>(&edit)) {
      scene.setMapObjectFlag(flag->object, flag->flag);
    } else if (const auto* rect = std::get_if<session::StoryBlockedRect>(&edit)) {
      scene.blockCells({.column = rect->column, .row = rect->row}, rect->width, rect->height);
    }
  }
}

} // namespace dwe::app
