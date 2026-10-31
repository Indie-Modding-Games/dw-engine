#include "field_story_props.hpp"

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/digimon_table.hpp>
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

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "boss_arrival_scene.hpp"
#include "field_scene.hpp"
#include "scene_prims.hpp"

namespace dwe::app {

namespace {

// The models map_object.c loads: loadDirtCartModel (GENERAL_OBJECTS 0 and 1), loadDirtPileModel,
// spawnBoulder.
constexpr std::array<std::string_view, 2> CartModels{"model/etcna/dosya", "model/etcna/hako"};
constexpr std::string_view PileModel = "model/etcna/t_yama";
constexpr std::string_view BoulderModel = "model/etcna/iwa";
// map_object.c: spawnAngemonPedestal (ANGEMON_PEDESTAL_TMD).
constexpr std::string_view PedestalModel = "model/etcna/abox";
// map_object.c: spawnGearbox (GEARBOX_TMD, GEARBOX_LOCATION) and spawnToyTownBoxes (BIG_BOX_TMD,
// SMALL_BOX_TMD and their locations). Each box is two linked objects, the box and its lid.
constexpr std::string_view GearboxModel = "model/etcna/gand";
constexpr glm::ivec3 GearboxLocation{-100, 0, 1700};
constexpr std::string_view BigBoxModel = "model/etcna/bigbox";
constexpr glm::ivec3 BigBoxLocation{500, -600, 600};
constexpr std::string_view SmallBoxModel = "model/etcna/smabox";
constexpr glm::ivec3 SmallBoxLocation{-400, -400, 600};
// map_object.c: DIRT_PILE_LOCATION.
constexpr glm::ivec3 PileLocation{2037, 0, 849};
// map_object.c: initializeTrainingPoop (session::trainingPoopOn).
constexpr std::string_view TrainingPoopModel = "model/etcna/try";
// renderDirtCartModel puts the cart the partner's radius plus 100 in front of it.
constexpr float CartReach = 100.0F;

// The original's field runs at 20 frames a second (main.c: gameLoop waits VSync(3)).
constexpr double OriginalFrameSeconds = 1.0 / 20.0;
// renderMonochromonMoodBubble's GsSPRITE: the faces of the image at VRAM (640, 0) with the CLUT at
// (0, 486), the first 4-bit image of the map and its first palette row (overworld.c:
// loadMapImage2); five faces of 32 x 32 in a row.
constexpr std::string_view MoodBubbleImage = "map/{}/texture4/000";
constexpr std::size_t MoodBubblePalette = 0;
constexpr glm::vec2 MoodBubbleImageSize{5 * MoodBubble::Side, MoodBubble::Side};

float radiansOf(std::int32_t originalAngle) {
  return static_cast<float>(originalAngle) * 2.0F * std::numbers::pi_v<float> /
         static_cast<float>(core::OriginalAnglePerTurn);
}

// tickMeramonShake's POLY_FT4: the glow image at VRAM (832, 384), 8-bit, with the CLUT at (0,
// 0x1e7) (ETCDAT\SPOT256.TIM and its first palette), texels (64, 0) to (127, 63): a soft white
// disc. Its colour is MERAMON_SHAKE_RED (0, never written), _GREEN and _BLUE (0xff) over 0x80, and
// the tpage's semi-transparency is 2, the background minus the texel: cyan taken away, the screen
// red.
constexpr std::string_view ShakeImage = "texture/etcdat/spot256";
constexpr glm::vec2 ShakeImageSize{128.0F, 128.0F};
constexpr glm::vec2 ShakeTexel{64.0F, 0.0F};
constexpr float ShakeTexels = 63.0F;
constexpr glm::vec4 ShakeColor{0.0F, 255.0F / 128.0F, 255.0F / 128.0F, 1.0F};
// The original's screen, in its pixels.
constexpr glm::vec2 OriginalScreen{320.0F, 240.0F};

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
  // A map change's fadeToBlack removes the fade to white (fade.c: removeObject(4005)) and fades out
  // from the shade it left.
  if (whiteFade_.active() && scene_->field().mapChangePending()) {
    core::logDebug("[port:story] fadeToBlack (fade.c:73): the map change ends the fade to white "
                   "at shade {}",
                   whiteFade_.shade());
    whiteFade_.stop();
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
  gearbox_ = false;
  toyBoxes_ = false;
  // The original's bubble object outlives the map, but every script takes it away before it ends
  // and the next map's VRAM holds other pictures: it goes with the map.
  moodBubble_.clear();
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
                   ScreenShake::Frames);
    shake_.start();
    shakeSeconds_ = 0.0;
  } else if (const auto* sprite = std::get_if<session::SpriteAtCharacter>(&request)) {
    placeSprite(*sprite);
  } else if (const auto* pedestal = std::get_if<session::AngemonPedestalShown>(&request)) {
    pedestal_ = *pedestal;
  } else if (std::holds_alternative<session::ToyTownGearboxShown>(request)) {
    gearbox_ = true;
  } else if (std::holds_alternative<session::ToyTownBoxesShown>(request)) {
    // spawnToyTownBoxes closes both lids and selects no box.
    toyBoxes_ = true;
    selectedBox_ = 0;
    bigLid_ = 0;
    smallLid_ = 0;
    lidSeconds_ = 0.0;
  } else if (const auto* box = std::get_if<session::ToyTownBoxSelected>(&request)) {
    selectedBox_ = box->box;
  } else if (const auto* fade = std::get_if<session::WhiteFadeStarted>(&request)) {
    startWhiteFade(*fade);
  } else if (const auto* bubble = std::get_if<session::MoodBubbleShown>(&request)) {
    takeMoodBubble(*bubble);
  } else if (const auto* boss = std::get_if<session::BossBuildupStarted>(&request);
             boss != nullptr && boss_ != nullptr) {
    boss_->startBuildup(*boss);
  }
}

std::optional<scene::Entity> FieldStoryProps::characterEntity(std::int64_t character) const {
  const game::field::Field& field = scene_->field();
  if (character == game::story::PlayerScriptId) {
    return field.playerEntity();
  }
  if (character == game::story::PartnerScriptId) {
    return field.partnerEntity();
  }
  if (std::in_range<std::int32_t>(character)) {
    return field.npcs().entityOf(static_cast<std::int32_t>(character));
  }
  return std::nullopt;
}

void FieldStoryProps::takeMoodBubble(const session::MoodBubbleShown& bubble) {
  if (!bubble.mood) {
    // removeObject(0x1b1, 0): one bubble object goes.
    moodBubble_.remove();
    return;
  }
  // createMonochromonMoodBubble adds the object only for a character on the map.
  if (!characterEntity(bubble.character)) {
    core::logDebug("[port:story] createMonochromonMoodBubble (script_common.c:3038): no "
                   "character {} on the map; no bubble",
                   bubble.character);
    return;
  }
  moodBubble_.show(bubble.character, *bubble.mood);
  bubbleSeconds_ = 0.0;
}

void FieldStoryProps::showMoodBubble() {
  std::vector<StorySprite> sprites;
  const std::optional<scene::Entity> entity =
      moodBubble_.shown() ? characterEntity(moodBubble_.character()) : std::nullopt;
  if (entity && species_ != nullptr) {
    // translateConditionFXToEntity: the condition point of the character's species.
    const game::field::Field& field = scene_->field();
    std::optional<std::string> speciesId;
    if (const auto instance = field.npcs().instanceOf(static_cast<std::int32_t>(
            std::clamp<std::int64_t>(moodBubble_.character(), 0, 0xFF)))) {
      if (const assets::MapNpc* record =
              field.npcs().record(*instance - game::field::FirstNpcInstance)) {
        speciesId = record->digimon;
      }
    }
    const assets::DigimonRow* row = speciesId ? species_->find(*speciesId) : nullptr;
    if (row != nullptr && row->conditionFx) {
      const auto& offset = row->conditionFx->offset;
      const std::optional<glm::vec3> point = scene_->entityNodePoint(
          *entity, row->conditionFx->node,
          glm::vec3{static_cast<float>(offset[0]), static_cast<float>(offset[1]),
                    static_cast<float>(offset[2])});
      if (point) {
        sprites.push_back({.point = *point,
                           .image = std::format(MoodBubbleImage, scene_->field().mapId()),
                           .palette = MoodBubblePalette,
                           .imageSize = MoodBubbleImageSize,
                           .texel = {static_cast<float>(moodBubble_.firstU()), 0.0F},
                           .side = glm::vec2{static_cast<float>(MoodBubble::Side)},
                           .scale = moodBubble_.scale(),
                           .lift = static_cast<float>(moodBubble_.offset())});
      }
    }
  }
  scene_->setStorySprites(std::move(sprites));
}

void FieldStoryProps::showShakeSquare() {
  std::vector<ScenePrim> prims;
  const render::MapCamera* camera = scene_->camera();
  const std::optional<rhi::TextureId> texture =
      shake_.squareShown() && camera != nullptr ? scene_->sceneImage(ShakeImage) : std::nullopt;
  if (texture) {
    // The square is placed against the drawing offset, so it shakes with the picture. On a screen
    // wider (or taller) than the original's it grows by as much, so the red reaches the edges as
    // it does on 320 x 240; at 4:3 it is the original's size.
    const glm::vec2 seen = camera->visibleSize() / OriginalScreen;
    const float half = static_cast<float>(shake_.squareHalf()) * std::max({1.0F, seen.x, seen.y});
    const glm::vec2 centre{static_cast<float>(shake_.offset()), 0.0F};
    const glm::vec2 from = ShakeTexel / ShakeImageSize;
    const glm::vec2 to = (ShakeTexel + glm::vec2{ShakeTexels}) / ShakeImageSize;
    // At the near plane in the screen's centre: the original sorts it at ordering-table entry 9,
    // in front of the map and the characters.
    const glm::vec4 screenCentre{0.0F, 0.0F, 0.0F, 1.0F};
    ScenePrim prim{.shape = ScenePrim::Shape::Quad,
                   .color = ShakeColor,
                   .texture = *texture,
                   .blend = ScenePrimBlend::Subtractive};
    prim.corners[0] = {
        .clip = screenCentre, .offset = centre + glm::vec2{-half, -half}, .uv = from};
    prim.corners[1] = {
        .clip = screenCentre, .offset = centre + glm::vec2{half, -half}, .uv = {to.x, from.y}};
    prim.corners[2] = {
        .clip = screenCentre, .offset = centre + glm::vec2{-half, half}, .uv = {from.x, to.y}};
    prim.corners[3] = {.clip = screenCentre, .offset = centre + glm::vec2{half, half}, .uv = to};
    prims.push_back(prim);
  }
  scene_->setStoryScreenPrims(std::move(prims));
}

void FieldStoryProps::startWhiteFade(const session::WhiteFadeStarted& fade) {
  game::field::Field& field = scene_->field();
  if (fade.toWhite) {
    // fadeToWhite leaves FADE_PROGRESS as the last fade left it.
    whiteFade_.fadeOut(fade.frames, field.shadeNow().value_or(0));
  } else {
    whiteFade_.fadeIn(fade.frames);
  }
  whiteSeconds_ = 0.0;
  field.setShade(whiteFade_.shade());
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
  core::logDebug("[port:story] spawn_sprite_at_entity (overworld.c:992): sprite {} at background "
                 "({}, {}), node {} of {} at ({:.0f}, {:.0f}, {:.0f})",
                 sprite.object, corner.x, corner.y, sprite.node, sprite.character, point->x,
                 point->y, point->z);
}

void FieldStoryProps::advance(double seconds) {
  advanceLids(seconds);
  if (moodBubble_.shown()) {
    bubbleSeconds_ += seconds;
    while (bubbleSeconds_ >= OriginalFrameSeconds) {
      bubbleSeconds_ -= OriginalFrameSeconds;
      moodBubble_.frame(characterEntity(moodBubble_.character()).has_value());
    }
  }
  if (whiteFade_.active()) {
    whiteSeconds_ += seconds;
    while (whiteFade_.active() && whiteSeconds_ >= OriginalFrameSeconds) {
      whiteSeconds_ -= OriginalFrameSeconds;
      whiteFade_.frame();
    }
    scene_->field().setShade(whiteFade_.shade());
  }
  if (!shake_.busy()) {
    return;
  }
  shakeSeconds_ += seconds;
  while (shake_.busy() && shakeSeconds_ >= OriginalFrameSeconds) {
    shakeSeconds_ -= OriginalFrameSeconds;
    shake_.frame();
  }
}

void FieldStoryProps::advanceLids(double seconds) {
  // renderToyTownBoxes turns the selected box's lid once a frame it draws the boxes.
  if (!toyBoxes_ ||
      (selectedBox_ != session::ToyTownBigBox && selectedBox_ != session::ToyTownSmallBox)) {
    lidSeconds_ = 0.0;
    return;
  }
  lidSeconds_ += seconds;
  while (lidSeconds_ >= OriginalFrameSeconds) {
    lidSeconds_ -= OriginalFrameSeconds;
    std::int32_t& lid = selectedBox_ == session::ToyTownBigBox ? bigLid_ : smallLid_;
    lid = session::toyTownLidAfterFrame(lid);
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
  // renderGearbox: unturned at its place.
  std::vector<glm::mat4> gearbox;
  if (gearbox_) {
    gearbox.push_back(glm::translate(glm::mat4{1.0F}, assets::toWorldPosition(GearboxLocation)));
  }
  setProps(*scene_, GearboxModel, std::move(gearbox));
  // renderToyTownBoxes: each box at its place, its lid (the second object, linked to the box)
  // turned about its x axis by its progress.
  const auto box = [this](const glm::ivec3 location, std::int32_t lid) {
    std::vector<FieldScene::PropPlace> places;
    if (toyBoxes_) {
      places.push_back({.world = glm::translate(glm::mat4{1.0F}, assets::toWorldPosition(location)),
                        .joints = {glm::mat4{1.0F}, glm::rotate(glm::mat4{1.0F}, radiansOf(lid),
                                                                glm::vec3{1.0F, 0.0F, 0.0F})},
                        .tint = glm::vec4{1.0F}});
    }
    return places;
  };
  if (const auto id = modelId(BigBoxModel)) {
    scene_->setPosedProps(*id, box(BigBoxLocation, bigLid_));
  }
  if (const auto id = modelId(SmallBoxModel)) {
    scene_->setPosedProps(*id, box(SmallBoxLocation, smallLid_));
  }
  // renderTrainingPoop: drawn on its map while the map layer is (FieldScene leaves props out when
  // the scenery is hidden).
  std::vector<glm::mat4> poop;
  if (const auto place = session::trainingPoopOn(field.mapId())) {
    poop.push_back(
        glm::translate(glm::mat4{1.0F}, assets::toWorldPosition({place->x, place->y, place->z})));
  }
  setProps(*scene_, TrainingPoopModel, std::move(poop));
  scene_->setScreenShake({static_cast<float>(shake_.offset()), 0.0F});
  showShakeSquare();
  showMoodBubble();
}

} // namespace dwe::app
