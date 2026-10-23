#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/field/field.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "field_scene.hpp"
#include "messages.hpp"

namespace dwe::app {

void FieldScene::placeMapObjects(const assets::MapEntry* entry) {
  const assets::MapDoorSet* doors =
      entry != nullptr && mapTable_ ? mapTable_->doorsOf(*entry) : nullptr;
  field_->placeMapObjects(doors);
  for (const auto& [model, handle] : mapObjectModels_) {
    models_->remove(handle);
  }
  mapObjectModels_.clear();
  for (const std::optional<game::field::MapObject3D>& object : field_->mapObjects()) {
    if (!object || mapObjectModels_.contains(object->model)) {
      continue;
    }
    const std::string name = core::formatMessage(messages::DoorModelId, object->model);
    auto id = assets::AssetId::parse(name);
    if (!id) {
      core::logWarning("{}",
                       core::formatMessage(messages::DoorModelMissing, name, id.error().message));
      continue;
    }
    auto model = assets::loadModel(*files_, *id);
    if (!model) {
      core::logWarning(
          "{}", core::formatMessage(messages::DoorModelMissing, name, model.error().message));
      continue;
    }
    auto handle = models_->add(*model);
    if (!handle) {
      core::logWarning(
          "{}", core::formatMessage(messages::DoorModelMissing, name, handle.error().message));
      continue;
    }
    mapObjectModels_.emplace(object->model, *handle);
  }
}

void FieldScene::submitMapObjects() {
  for (const std::optional<game::field::MapObject3D>& object : field_->mapObjects()) {
    if (!object) {
      continue;
    }
    const auto found = mapObjectModels_.find(object->model);
    if (found == mapObjectModels_.end()) {
      continue;
    }
    const glm::mat4 world = glm::rotate(glm::translate(glm::mat4{1.0F}, object->position),
                                        object->yaw, glm::vec3{0.0F, 1.0F, 0.0F});
    models_->submit({.model = found->second, .world = world, .joints = {}});
  }
}

void FieldScene::setProps(const assets::AssetId& model, std::vector<glm::mat4> places) {
  props_ = std::move(places);
  if (propModelId_ == model) {
    return;
  }
  if (propModel_) {
    models_->remove(*propModel_);
    propModel_.reset();
  }
  propModelId_ = model;
  auto data = assets::loadModel(*files_, model);
  if (!data) {
    core::logWarning(
        "{}", core::formatMessage(messages::PropModelMissing, model.str(), data.error().message));
    return;
  }
  auto handle = models_->add(*data);
  if (!handle) {
    core::logWarning(
        "{}", core::formatMessage(messages::PropModelMissing, model.str(), handle.error().message));
    return;
  }
  propModel_ = *handle;
}

void FieldScene::submitProps() {
  if (!propModel_) {
    return;
  }
  for (const glm::mat4& place : props_) {
    models_->submit({.model = *propModel_, .world = place, .joints = {}});
  }
}

} // namespace dwe::app
