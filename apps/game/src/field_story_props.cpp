#include "field_story_props.hpp"

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/math.hpp>
#include <dwe/game/field/field.hpp>
#include <dwe/game/story/script_motion.hpp>
#include <dwe/render/map_camera.hpp>
#include <dwe/render/map_layers.hpp>
#include <dwe/scene/components.hpp>
#include <dwe/session/story_scenes.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstddef>
#include <numbers>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "field_scene.hpp"

namespace dwe::app {

namespace {

// The models map_object.c loads: loadDirtCartModel (GENERAL_OBJECTS 0 and 1), loadDirtPileModel,
// spawnBoulder.
constexpr std::array<std::string_view, 2> CartModels{"model/etcna/dosya", "model/etcna/hako"};
constexpr std::string_view PileModel = "model/etcna/t_yama";
constexpr std::string_view BoulderModel = "model/etcna/iwa";
// map_object.c: spawnAngemonPedestal (ANGEMON_PEDESTAL_TMD).
constexpr std::string_view PedestalModel = "model/etcna/abox";
// map_object.c: DIRT_PILE_LOCATION.
constexpr glm::ivec3 PileLocation{2037, 0, 849};
// map_object.c: initializeTrainingPoop (session::trainingPoopOn).
constexpr std::string_view TrainingPoopModel = "model/etcna/try";
// renderDirtCartModel puts the cart the partner's radius plus 100 in front of it.
constexpr float CartReach = 100.0F;

// The original's field runs at 20 frames a second (main.c: gameLoop waits VSync(3)).
constexpr double OriginalFrameSeconds = 1.0 / 20.0;
// map.c: tickMeramonShake removes itself on its 210th frame and puts the drawing offset back.
constexpr std::int32_t ShakeFrames = 0xD2;

// The step of the shake's drawing offset on a frame: 1 pixel, 2 from frame 31, 3 from 61, 2 from
// 121 and 1 from 181, away on the even frames and back on the odd ones.
std::int32_t shakeStep(std::int32_t frame) {
  std::int32_t step = 1;
  if (frame >= 0xB5) {
    step = 1;
  } else if (frame >= 0x79) {
    step = 2;
  } else if (frame >= 0x3D) {
    step = 3;
  } else if (frame >= 0x1F) {
    step = 2;
  }
  return frame % 2 == 0 ? -step : step;
}

float radiansOf(std::int32_t originalAngle) {
  return static_cast<float>(originalAngle) * 2.0F * std::numbers::pi_v<float> /
         static_cast<float>(core::OriginalAnglePerTurn);
}

// The ids are constants that parse; one that did not would only draw nothing.
std::optional<assets::AssetId> modelId(std::string_view id) {
  auto parsed = assets::AssetId::parse(id);
  return parsed ? std::optional{std::move(*parsed)} : std::nullopt;
}

void setProps(FieldScene& scene, std::string_view model, std::vector<glm::mat4> places) {
  if (const auto id = modelId(model)) {
    scene.setProps(*id, std::move(places));
  }
}

} // namespace

FieldStoryProps::FieldStoryProps(FieldScene& scene) : scene_{&scene} {}

void FieldStoryProps::update(session::StoryScenes& scenes) {
  const std::string& mapId = scene_->field().mapId();
  if (mapId != mapId_) {
    mapId_ = mapId;
    clearMapParts();
  }
  while (const auto request = scenes.takePropRequest()) {
    take(*request);
  }
}

void FieldStoryProps::clearMapParts() {
  cartModel_.reset();
  pileSize_.reset();
  boulder_.reset();
  pedestal_.reset();
}

void FieldStoryProps::take(const session::StoryPropRequest& request) {
  if (const auto* cart = std::get_if<session::DirtCartShown>(&request)) {
    cartModel_ = cart->model;
  } else if (const auto* pile = std::get_if<session::DirtPileShown>(&request)) {
    pileSize_ = pile->size;
  } else if (const auto* boulder = std::get_if<session::BoulderShown>(&request)) {
    if (boulder->shown) {
      boulder_ = *boulder;
    } else {
      boulder_.reset();
    }
  } else if (std::holds_alternative<session::ScreenShakeStarted>(request)) {
    // createMeramonShake starts over from frame 0 and the offset the drawing has now.
    core::logDebug("[port:story] createMeramonShake (map.c:5472): the screen shakes {} frames",
                   ShakeFrames);
    shakeFrame_ = 0;
    shakeOffset_ = 0;
    shakeSeconds_ = 0.0;
  } else if (const auto* sprite = std::get_if<session::SpriteAtCharacter>(&request)) {
    placeSprite(*sprite);
  } else if (const auto* pedestal = std::get_if<session::AngemonPedestalShown>(&request)) {
    pedestal_ = *pedestal;
  }
}

void FieldStoryProps::placeSprite(const session::SpriteAtCharacter& sprite) {
  const game::field::Field& field = scene_->field();
  std::optional<scene::Entity> entity;
  if (sprite.character == game::story::PlayerScriptId) {
    entity = field.playerEntity();
  } else if (sprite.character == game::story::PartnerScriptId) {
    entity = field.partnerEntity();
  } else if (std::in_range<std::int32_t>(sprite.character)) {
    entity = field.npcs().entityOf(static_cast<std::int32_t>(sprite.character));
  }
  const render::MapCamera* camera = scene_->camera();
  const auto object = static_cast<std::size_t>(sprite.object);
  const std::optional<glm::vec3> point =
      entity && sprite.node >= 0
          ? scene_->entityNodePoint(*entity, static_cast<std::uint32_t>(sprite.node))
          : std::nullopt;
  const auto pixel =
      point && camera != nullptr ? camera->projectToBackground(*point) : std::nullopt;
  const std::optional<glm::vec2> size = scene_->mapLayers().spriteSize(object);
  if (!pixel || !size) {
    core::logDebug("[port:story] spawn_sprite_at_entity (overworld.c:992): no node {} of {} or "
                   "no sprite {} on the map; nothing moves",
                   sprite.node, sprite.character, sprite.object);
    return;
  }
  // spawnSpriteAtLocation centres the sprite on the point, in whole pixels.
  const glm::vec2 corner = glm::floor(pixel->pixel) - glm::floor(*size / 2.0F);
  scene_->mapLayers().setPosition(object, corner);
}

void FieldStoryProps::advance(double seconds) {
  if (!shakeFrame_) {
    return;
  }
  shakeSeconds_ += seconds;
  while (shakeFrame_ && shakeSeconds_ >= OriginalFrameSeconds) {
    shakeSeconds_ -= OriginalFrameSeconds;
    shakeOffset_ += shakeStep(*shakeFrame_);
    ++*shakeFrame_;
    if (*shakeFrame_ >= ShakeFrames) {
      shakeFrame_.reset();
      shakeOffset_ = 0;
    }
  }
}

void FieldStoryProps::show() {
  const game::field::Field& field = scene_->field();
  // renderDirtCartModel: model 0 or 1, the partner's radius plus 100 ahead of it, turned as it is,
  // on the ground (y 0).
  for (std::size_t model = 0; model < CartModels.size(); ++model) {
    std::vector<glm::mat4> places;
    if (cartModel_ && std::cmp_equal(*cartModel_, model)) {
      const game::field::CharacterState& partner = field.partner();
      const glm::mat4 turn = glm::rotate(glm::mat4{1.0F}, partner.yaw, glm::vec3{0.0F, 1.0F, 0.0F});
      const float reach = field.rules().partner.mover.radius + CartReach;
      // The original's +z ahead is the world's -z (FieldScene::nodePoint).
      const glm::vec3 ahead = glm::vec3{turn * glm::vec4{0.0F, 0.0F, -reach, 0.0F}};
      const glm::vec3 place{partner.position.x + ahead.x, 0.0F, partner.position.z + ahead.z};
      places.push_back(glm::translate(glm::mat4{1.0F}, place) * turn);
    }
    setProps(*scene_, CartModels.at(model), std::move(places));
  }
  // renderDirtPile and loadDirtPileModel: scaled by size / 15, drawn from size 6 up.
  std::vector<glm::mat4> pile;
  if (pileSize_ && *pileSize_ >= session::DirtPileSmallestShown) {
    const float scale =
        static_cast<float>(*pileSize_) / static_cast<float>(session::DirtPileFullSize);
    pile.push_back(glm::scale(
        glm::translate(glm::mat4{1.0F}, assets::toWorldPosition(PileLocation)), glm::vec3{scale}));
  }
  setProps(*scene_, PileModel, std::move(pile));
  // projectPosition: turned about x, then placed.
  std::vector<glm::mat4> boulder;
  if (boulder_) {
    const glm::vec3 place = assets::toWorldPosition({boulder_->x, boulder_->y, boulder_->z});
    boulder.push_back(glm::rotate(glm::translate(glm::mat4{1.0F}, place),
                                  radiansOf(boulder_->pitch), glm::vec3{1.0F, 0.0F, 0.0F}));
  }
  setProps(*scene_, BoulderModel, std::move(boulder));
  // renderAngemonPedestal: unturned at (x, 0, z), while the map layer is drawn.
  std::vector<glm::mat4> pedestal;
  if (pedestal_) {
    pedestal.push_back(
        glm::translate(glm::mat4{1.0F}, assets::toWorldPosition({pedestal_->x, 0, pedestal_->z})));
  }
  setProps(*scene_, PedestalModel, std::move(pedestal));
  // renderTrainingPoop: drawn on its map while the map layer is (FieldScene leaves props out when
  // the scenery is hidden).
  std::vector<glm::mat4> poop;
  if (const auto place = session::trainingPoopOn(field.mapId())) {
    poop.push_back(
        glm::translate(glm::mat4{1.0F}, assets::toWorldPosition({place->x, place->y, place->z})));
  }
  setProps(*scene_, TrainingPoopModel, std::move(poop));
  scene_->setScreenShake({static_cast<float>(shakeOffset_), 0.0F});
}

} // namespace dwe::app
