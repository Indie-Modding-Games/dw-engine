#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/scene/components.hpp>

#include <memory>
#include <span>
#include <string_view>
#include <utility>

#include "field_scene.hpp"
#include "messages.hpp"

namespace dwe::app {

namespace {

constexpr std::string_view SpriteCharactersId = "field/sprite_characters";

} // namespace

SpriteCharacterTable loadSpriteCharacters(const assets::VirtualFileSystem& files) {
  auto id = assets::AssetId::parse(SpriteCharactersId);
  if (!id || files.resolve(*id) == nullptr) {
    core::logWarning("{}",
                     core::formatMessage(messages::SpriteCharactersMissing, SpriteCharactersId));
    return {};
  }
  auto bytes = files.read(*id);
  if (!bytes) {
    core::logWarning("{}", core::formatMessage(messages::SpriteCharactersUnreadable,
                                               SpriteCharactersId, bytes.error().message));
    return {};
  }
  auto table = parseSpriteCharacters(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  if (!table) {
    core::logWarning("{}", core::formatMessage(messages::SpriteCharactersUnreadable,
                                               SpriteCharactersId, table.error().message));
    return {};
  }
  return std::move(*table);
}

void FieldScene::setModelCharacters(bool on) {
  if (on == modelCharacters_) {
    return;
  }
  modelCharacters_ = on;
  standInSpriteCharacters(map_.document.id);
}

void FieldScene::standInSpriteCharacters(std::string_view mapId) {
  render::MapDepthLayers& layers = mapRenderer_->layers();
  for (StandIn& standIn : standIns_) {
    standInFor(std::span{&standIn.character, 1}, layers, false);
    models_->remove(standIn.actor->handle);
  }
  standIns_.clear();
  if (!modelCharacters_ || !mapRenderer_->hasMap()) {
    return;
  }
  const game::field::NpcRules& rules = field_->rules().npc;
  for (const SpriteCharacter& character : spriteCharacterTable_.of(mapId)) {
    const std::string model = speciesModelOf(character.species);
    auto modelId = assets::AssetId::parse(model);
    auto actor = modelId ? loadActor(NpcCharacter, scene::Entity{},
                                     {.model = model,
                                      .radius = 0.0F,
                                      .walkSpeed = rules.walkSpeed,
                                      .runSpeed = rules.runSpeed,
                                      .turnSpeed = rules.turnSpeed},
                                     *modelId, MissingClips::Drop)
                         : core::Result<std::unique_ptr<Actor>>{std::unexpected{modelId.error()}};
    if (!actor) {
      core::logWarning("{}", core::formatMessage(messages::SpriteCharacterModelMissing,
                                                 character.species, mapId, actor.error().message));
      continue;
    }
    Actor& standIn = **actor;
    if (const std::optional<std::uint32_t> idle =
            standIn.character.clipOf(scene::motionName(scene::Motion::Idle))) {
      standIn.motion = scene::Motion::Idle;
      standIn.motionClip = idle;
      standIn.player->play(*idle, 0.0F);
    }
    standInFor(std::span{&character, 1}, layers, true);
    standIns_.push_back({.character = character, .actor = std::move(*actor)});
  }
}

void FieldScene::drawSpriteCharacters(double animationSeconds) {
  // A stage camera draws neither the background nor its sprites.
  if (standIns_.empty() || stage_.camera) {
    return;
  }
  const render::MapDepthLayers& layers = mapRenderer_->layers();
  const render::MapCamera& camera = mapRenderer_->camera();
  for (StandIn& standIn : standIns_) {
    if (!shows(standIn.character, layers)) {
      continue;
    }
    const std::optional<SpriteCharacterPlace> place = placeOf(standIn.character, layers, camera);
    if (!place) {
      continue;
    }
    Actor& actor = *standIn.actor;
    static_cast<void>(actor.player->advance(static_cast<float>(animationSeconds)));
    const glm::mat4 world = glm::rotate(glm::translate(glm::mat4{1.0F}, place->feet), place->yaw,
                                        glm::vec3{0.0F, 1.0F, 0.0F});
    const std::optional<float> additive = standIn.character.additive;
    const glm::vec4 strength{glm::vec3{additive.value_or(1.0F)}, 1.0F};
    models_->submit({.model = actor.handle,
                     .world = world,
                     .joints = drawnJoints(actor),
                     .tint = sceneryTint() * strength,
                     .additive = additive.has_value()});
  }
}

} // namespace dwe::app
