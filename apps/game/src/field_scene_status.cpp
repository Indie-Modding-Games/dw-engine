#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/image_palettes.hpp>
#include <dwe/assets/indexed_image.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_images.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/yaml.hpp>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "field_scene.hpp"

// The status visuals over the fighters (battle_effect.c: BTL_renderPoisonBubble,
// BTL_renderConfusionEffect, BTL_renderStunEffect, BTL_renderStunSubEffect), drawn from the
// battle's game::battle::StatusVisuals. The original's axes are the world's with y and z the other
// way; its scales are 4096 for 1, its rotations 4096 a turn and math.c's _sin and _cos 512.
namespace dwe::app {

namespace {

using game::battle::StatusVisuals;

constexpr float One = 4096.0F;
constexpr float Turn = 4096.0F;
constexpr float SineTurn = 512.0F;
constexpr float Percent = 100.0F;

// ETCDAT/FEEL_EF.TIM: the bubble's six frames, 16 x 16 from u 0x20 on row 48, palette 7, added
// (BTL_POISON_BUBBLE_SPRITE: attribute 0x50000000), the frame changing every 2 frames
// (BTL_POISON_BUBBLE_FRAME_U).
constexpr std::string_view BubbleImage = "texture/etcdat/feel_ef";
constexpr std::int32_t BubblePalette = 7;
constexpr glm::vec2 BubbleTexels{16.0F, 16.0F};
constexpr float BubbleRow = 48.0F;
constexpr float BubbleFirstU = 0x20;
constexpr std::int32_t BubbleFrameCount = 6;
constexpr float BubbleFrameWidth = 16.0F;
// renderSprite(..., 0x4EA4, 0x4EA4): scale 0x4EA4 * VIEWPORT_DISTANCE / depth, so a texel is
// 0x4EA4 / 4096 world units whatever the depth.
constexpr float BubbleTexelUnits = 0x4EA4 / One;
// It rises from 50 to 200 units over the condition point and sways 20 units across the screen,
// 0x500 of _sin (two and a half turns) over its 40 frames; drawn only between depths 0x200 and
// 0x10000.
constexpr float BubbleRiseFrom = 0x32;
constexpr float BubbleRiseTo = 0xC8;
constexpr float BubbleSwayTurn = 0x500;
constexpr float BubbleSway = 0x14;
constexpr float NearestDepth = 0x200;
constexpr float FarthestDepth = 0x10000;

// model/etcdat/feel_ef: three objects 0x78 units around and 0x78 over the condition point, a
// third of a turn apart (0xAA of _sin), each turned by its roll, the spin and a third more (0x555),
// tilted 0xE3 about z (BTL_renderConfusionEffect; BTL_CONFUSION_SCALE is 1).
constexpr std::string_view ConfusionModel = "model/etcdat/feel_ef";
constexpr float ConfusionRadius = 0x78;
constexpr float ConfusionLift = 0x78;
constexpr std::int32_t ConfusionApart = 0xAA;
constexpr std::int32_t ConfusionFacing = -0x400;
constexpr std::int32_t ConfusionThird = 0x555;
constexpr std::int32_t ConfusionTilt = 0xE3;

// model/etchi/mahi: a ring that falls from the sign's height to the feet over 15 frames, as wide as
// the species' radius (radius * 0x4000 / 350, from a tenth over its first 4 frames) and 3 high
// (BTL_STUN_SUB_EFFECT_SCALE 0x3000).
constexpr std::string_view StunRingModel = "model/etchi/mahi";
constexpr float StunRingFall = 0xF;
constexpr float StunRingWidth = 0x4000 / 350.0F;
constexpr float StunRingHeight = 0x3000 / One;
constexpr float StunRingGrowFrames = 4;
constexpr float StunRingFirstWidth = 0.1F;

// ETCDAT/BI_IN.TIM: the sign over the fighter at 113 % of its height, sprites of fixed size on
// screen (GsSortSprite, 1 texel a pixel), added: the stun mark (palette 0, 1 on odd frames of the
// flash) and the countdown, two big digits and two small ones of (duration - frame) * 100 / 20
// (BTL_STUN_DIGIT_SPRITE, BTL_STUN_SMALL_DIGIT_SPRITE, BTL_D_8007376C, BTL_D_80073790).
constexpr std::string_view SignImage = "texture/etcdat/bi_in";
constexpr float SignLift = 113.0F;
constexpr std::int32_t DigitPalette = 2;
struct SignSprite {
  glm::vec2 texel;
  glm::vec2 texels;
  glm::vec2 pivot;
};
constexpr SignSprite MarkLeft{.texel = {0, 0}, .texels = {40, 22}, .pivot = {20, 44}};
constexpr SignSprite MarkRight{.texel = {40, 0}, .texels = {12, 22}, .pivot = {6, 22}};
constexpr glm::vec2 BigDigit{8, 9};
constexpr float BigDigitRow = 55;
constexpr std::array<float, 2> BigDigitPivotX{14, 7};
constexpr float BigDigitPivotY = 38;
constexpr glm::vec2 SmallDigit{4, 7};
constexpr float SmallDigitRow = 22;
constexpr std::array<float, 2> SmallDigitPivotX{-2, -8};
constexpr float SmallDigitPivotY = 37;
constexpr std::int32_t CountdownPerFrame = 100;
constexpr std::int32_t CountdownFrames = 20;
constexpr float ShrinkTo = 0xCC;

glm::vec3 worldOf(glm::vec3 original) {
  return {original.x, -original.y, -original.z};
}

float radiansOf(float angle) {
  return angle * 2.0F * std::numbers::pi_v<float> / Turn;
}
float sineRadiansOf(float angle) {
  return angle * 2.0F * std::numbers::pi_v<float> / SineTurn;
}

// effect_common.c: lerp from `from` at frame 0 to `to` at `frames`.
float lerp(float from, float to, float frames, float frame) {
  return from + ((to - from) * frame / frames);
}

// libgte RotMatrix(0, y, z) = Ry · Rz in the original's axes, then F M F with F = diag(1, -1, -1)
// in the world's.
glm::mat4 rotationYZ(std::int32_t y, std::int32_t z) {
  const float sy = std::sin(radiansOf(static_cast<float>(y)));
  const float cy = std::cos(radiansOf(static_cast<float>(y)));
  const float sz = std::sin(radiansOf(static_cast<float>(z)));
  const float cz = std::cos(radiansOf(static_cast<float>(z)));
  const std::array<std::array<float, 3>, 3> ry{{{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}}};
  const std::array<std::array<float, 3>, 3> rz{{{cz, -sz, 0}, {sz, cz, 0}, {0, 0, 1}}};
  constexpr std::array<float, 3> Flip{1.0F, -1.0F, -1.0F};
  glm::mat4 out{1.0F};
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      float sum = 0.0F;
      for (std::size_t k = 0; k < 3; ++k) {
        sum += ry.at(row).at(k) * rz.at(k).at(column);
      }
      out[static_cast<glm::length_t>(column)][static_cast<glm::length_t>(row)] =
          Flip.at(row) * Flip.at(column) * sum;
    }
  }
  return out;
}

// An image with a row of its CLUT, the colours with STP at SemiTransparentAlpha so an added sprite
// mixes them (colorFromOriginal), and how many of its texels make one of the original's (a skin's
// pack may hold it larger: the metadata keeps the original's width).
struct StatusImage {
  assets::Image image;
  float texelScale = 1.0F;
};

core::Result<StatusImage> statusImage(const assets::VirtualFileSystem& files,
                                      std::string_view image, std::int32_t palette) {
  auto png = assets::AssetId::parse(image);
  auto meta = assets::AssetId::parse(std::format("{}{}", image, assets::ImageMetaSuffix));
  if (!png || !meta) {
    return std::unexpected{!png ? png.error() : meta.error()};
  }
  auto pngBytes = files.read(*png);
  auto metaBytes = files.read(*meta);
  if (!pngBytes || !metaBytes) {
    return std::unexpected{!pngBytes ? pngBytes.error() : metaBytes.error()};
  }
  auto indexed = assets::decodeIndexedPng(*pngBytes);
  if (!indexed) {
    return std::unexpected{indexed.error()};
  }
  const std::string_view metaYaml{reinterpret_cast<const char*>(metaBytes->data()),
                                  metaBytes->size()};
  auto palettes = assets::parseImagePalettes(metaYaml);
  if (!palettes) {
    return std::unexpected{palettes.error()};
  }
  if (palette < 0 || std::cmp_greater_equal(palette, palettes->size())) {
    return core::fail(std::format("no palette {} of {}", palette, palettes->size()));
  }
  float texelScale = 1.0F;
  if (auto root = core::parseYaml(metaYaml)) {
    if (const core::YamlNode* width = root->find("width"); width != nullptr && width->isScalar()) {
      std::uint32_t original = 0;
      const std::string& text = width->text();
      const auto [last, error] = std::from_chars(text.data(), text.data() + text.size(), original);
      if (error == std::errc{} && original > 0) {
        texelScale = static_cast<float>(indexed->width) / static_cast<float>(original);
      }
    }
  }
  std::vector<assets::PaletteColor> colors;
  for (const std::uint16_t color : palettes->at(static_cast<std::size_t>(palette))) {
    colors.push_back(assets::colorFromOriginal(color));
  }
  return StatusImage{.image = assets::applyPalette(*indexed, colors), .texelScale = texelScale};
}

} // namespace

std::optional<FieldScene::StatusTexture> FieldScene::statusTexture(std::string_view image,
                                                                   std::int32_t palette) {
  const auto key = std::pair{std::string{image}, palette};
  if (const auto found = statusTextures_.find(key); found != statusTextures_.end()) {
    return found->second;
  }
  std::optional<StatusTexture> texture;
  auto loaded = statusImage(*files_, image, palette);
  if (loaded) {
    if (auto added = mapRenderer_->addBillboardTexture(loaded->image)) {
      texture = StatusTexture{.texture = *added, .texelScale = loaded->texelScale};
    } else {
      loaded = std::unexpected{added.error()};
    }
  }
  if (!loaded) {
    core::logWarning("battle: no image {} (palette {}) for the status visuals: {}", image, palette,
                     loaded.error().message);
  }
  statusTextures_.emplace(key, texture);
  return texture;
}

const FieldScene::EffectModelEntry* FieldScene::statusModel(std::string_view model) {
  if (const auto found = statusModels_.find(model); found != statusModels_.end()) {
    return found->second ? &*found->second : nullptr;
  }
  std::optional<EffectModelEntry> entry;
  auto id = assets::AssetId::parse(model);
  auto data = id ? assets::loadModel(*files_, *id)
                 : core::Result<assets::ModelData>{std::unexpected{id.error()}};
  if (data) {
    if (auto handle = models_->add(*data)) {
      entry =
          EffectModelEntry{.handle = *handle, .joints = data->skin ? data->skin->joints.size() : 0};
    } else {
      data = std::unexpected{handle.error()};
    }
  }
  if (!data) {
    core::logWarning("battle: no model {} for the status visuals: {}", model, data.error().message);
  }
  auto [at, inserted] = statusModels_.emplace(std::string{model}, entry);
  return at->second ? &*at->second : nullptr;
}

std::optional<FieldScene::StatusAnchor> FieldScene::statusAnchor(std::size_t fighter) const {
  if (fighter >= statusBodies_.size()) {
    return std::nullopt;
  }
  const StatusBody& body = statusBodies_[fighter];
  const Actor* actor = fighterActor(body.npc);
  if (actor == nullptr) {
    return std::nullopt;
  }
  const glm::vec3 feet = heldPlace(*actor).position;
  // translateConditionFXToEntity: the node of CONDITION_FX_OFFSETS with its offset turned by the
  // node's matrix; a species without one (a pack's new species) hangs them over its height.
  const glm::vec3 top = feet + glm::vec3{0.0F, static_cast<float>(body.height), 0.0F};
  const std::optional<glm::vec3> condition =
      body.conditionFx ? nodePoint(*actor, body.conditionFx->first, body.conditionFx->second)
                       : std::nullopt;
  return StatusAnchor{.feet = feet, .condition = condition.value_or(top), .body = body};
}

// model.c: renderFlatDigimon: a flattened fighter is a 15 x 15 sprite of its own texture (u 0xE0
// or 0xF0, v 0x60 for the walking frames and 0x70 for the shot and the hurt one) in its CLUT's
// last row (clutPage + 0x3C0: black and white), on an upright card as tall and as wide as its
// species, along its facing, scaled as its body.
constexpr std::int32_t FlatPalette = 15;
constexpr float FlatFirstU = 0xE0;
constexpr float FlatSecondU = 0xF0;
constexpr float FlatWalkV = 0x60;
constexpr float FlatPoseV = 0x70;
constexpr glm::vec2 FlatTexels{15.0F, 15.0F};
constexpr std::string_view SpeciesTextureLeaf = "texture";

void FieldScene::addFlatFighterBillboards(std::vector<render::MapBillboard>& billboards) {
  for (const StatusBody& body : statusBodies_) {
    const Actor* actor = fighterActor(body.npc);
    if (actor == nullptr || !actor->fighter || !actor->fighter->flatSprite) {
      continue;
    }
    const std::string& model = actor->character.model.str();
    const std::size_t leaf = model.rfind('/');
    if (leaf == std::string::npos) {
      continue;
    }
    const std::string image = std::format("{}/{}", model.substr(0, leaf), SpeciesTextureLeaf);
    const std::optional<StatusTexture> texture = statusTexture(image, FlatPalette);
    if (!texture) {
      continue;
    }
    using game::battle::FlatSprite;
    const FlatSprite sprite = *actor->fighter->flatSprite;
    const float u =
        sprite == FlatSprite::Walk || sprite == FlatSprite::Shot ? FlatFirstU : FlatSecondU;
    const float v =
        sprite == FlatSprite::Walk || sprite == FlatSprite::WalkAlt ? FlatWalkV : FlatPoseV;
    const auto& transform = world_.registry().get<scene::Transform>(actor->entity);
    const glm::vec3 scale = actor->fighter->scale;
    const auto height = static_cast<float>(body.height);
    billboards.push_back(
        {.centre = transform.position + glm::vec3{0.0F, height * scale.y / 2.0F, 0.0F},
         .size = {height * scale.z, height * scale.y},
         .flat = false,
         .texture = texture->texture,
         .texel = glm::vec2{u, v} * texture->texelScale,
         .texels = FlatTexels * texture->texelScale,
         .blend = assets::SpriteBlend::Opaque,
         .color = glm::vec3{1.0F},
         .card = assets::forwardOfYaw(transform.yaw)});
  }
}

void FieldScene::addStatusBillboards(std::vector<render::MapBillboard>& billboards) {
  if (!statusVisuals_) {
    return;
  }
  const render::MapCamera& camera = mapRenderer_->camera();
  const glm::mat4& view = camera.view();
  const glm::vec3 right{view[0][0], view[1][0], view[2][0]};
  for (const auto& slot : statusVisuals_->bubbles()) {
    if (!slot) {
      continue;
    }
    const std::optional<StatusAnchor> anchor = statusAnchor(slot->fighter);
    const std::optional<StatusTexture> texture = statusTexture(BubbleImage, BubblePalette);
    if (!anchor || !texture) {
      continue;
    }
    const auto frame = static_cast<float>(slot->frame);
    const auto frames = static_cast<float>(StatusVisuals::BubbleFrames);
    const float sway =
        std::sin(sineRadiansOf(lerp(0.0F, BubbleSwayTurn, frames, frame))) * BubbleSway;
    const glm::vec3 centre = anchor->condition +
                             glm::vec3{static_cast<float>(slot->offsetX),
                                       lerp(BubbleRiseFrom, BubbleRiseTo, frames, frame),
                                       -static_cast<float>(slot->offsetZ)} +
                             (right * sway);
    const float depth = camera.viewDepth(centre);
    if (depth <= NearestDepth || depth >= FarthestDepth) {
      continue;
    }
    const float u = BubbleFirstU +
                    (BubbleFrameWidth * static_cast<float>((slot->frame >> 1) % BubbleFrameCount));
    billboards.push_back({.centre = centre,
                          .size = BubbleTexels * BubbleTexelUnits,
                          .flat = false,
                          .texture = texture->texture,
                          .texel = glm::vec2{u, BubbleRow} * texture->texelScale,
                          .texels = BubbleTexels * texture->texelScale,
                          .blend = assets::SpriteBlend::Add,
                          .color = glm::vec3{1.0F}});
  }
  for (const auto& slot : statusVisuals_->stuns()) {
    if (!slot) {
      continue;
    }
    const std::optional<StatusAnchor> anchor = statusAnchor(slot->fighter);
    if (!anchor) {
      continue;
    }
    std::int32_t value = 0;
    float scale = 1.0F;
    std::int32_t flash = 0;
    switch (slot->stage) {
    case StatusVisuals::StunStage::Countdown:
      value = (slot->duration - slot->frame) * CountdownPerFrame / CountdownFrames;
      break;
    case StatusVisuals::StunStage::Flash:
      flash = slot->frame % 2;
      break;
    case StatusVisuals::StunStage::Shrink:
      scale = lerp(One, ShrinkTo, static_cast<float>(StatusVisuals::StunShrinkFrames),
                   static_cast<float>(slot->frame)) /
              One;
      break;
    }
    // The sign's height is the body's over the ground, not over the feet (pos.vy is set, not
    // added).
    const glm::vec3 centre{anchor->feet.x,
                           static_cast<float>(anchor->body.height) * SignLift / Percent,
                           anchor->feet.z};
    const float depth = camera.viewDepth(centre);
    if (depth <= 0.0F) {
      continue;
    }
    // One screen pixel of the original at that depth, in world units.
    const float pixel = depth / camera.desc().focalLength * scale;
    // GsSortSprite puts the last sorted first on its ordering-table slot: the mark is drawn under
    // the countdown, which goes a pixel nearer the camera so the depth test keeps that order.
    const glm::vec3 eye{glm::inverse(view)[3]};
    const glm::vec3 nearer = glm::normalize(eye - centre) * pixel;
    const auto add = [&](std::int32_t palette, glm::vec2 texel, glm::vec2 texels, glm::vec2 pivot,
                         glm::vec3 at) {
      const std::optional<StatusTexture> texture = statusTexture(SignImage, palette);
      if (!texture) {
        return;
      }
      billboards.push_back({.centre = at,
                            .size = texels * pixel,
                            .flat = false,
                            .texture = texture->texture,
                            .texel = texel * texture->texelScale,
                            .texels = texels * texture->texelScale,
                            .blend = assets::SpriteBlend::Add,
                            .color = glm::vec3{1.0F},
                            .pivot = pivot / texels});
    };
    add(flash, MarkLeft.texel, MarkLeft.texels, MarkLeft.pivot, centre);
    add(flash, MarkRight.texel, MarkRight.texels, MarkRight.pivot, centre);
    constexpr std::int32_t Ten = 10;
    const std::array<std::int32_t, 2> big{value / 1000 % Ten, value / 100 % Ten};
    const std::array<std::int32_t, 2> small{value / Ten % Ten, value % Ten};
    for (std::size_t digit = 0; digit < 2; ++digit) {
      add(DigitPalette, {static_cast<float>(big.at(digit)) * BigDigit.x, BigDigitRow}, BigDigit,
          {BigDigitPivotX.at(digit), BigDigitPivotY}, centre + nearer);
    }
    for (std::size_t digit = 0; digit < 2; ++digit) {
      add(DigitPalette, {static_cast<float>(small.at(digit)) * SmallDigit.x, SmallDigitRow},
          SmallDigit, {SmallDigitPivotX.at(digit), SmallDigitPivotY}, centre + nearer);
    }
  }
}

void FieldScene::submitStatusModels() {
  if (!statusVisuals_) {
    return;
  }
  std::vector<glm::mat4> joints;
  const auto submit = [&](const EffectModelEntry& entry, glm::vec3 at, const glm::mat4& turn) {
    if (entry.joints == 0) {
      return;
    }
    joints.assign(entry.joints, glm::mat4{0.0F});
    joints.front() = turn;
    models_->submit({.model = entry.handle,
                     .world = glm::translate(glm::mat4{1.0F}, at),
                     .joints = joints,
                     .tint = glm::vec4{1.0F}});
  };
  if (const EffectModelEntry* model = statusModel(ConfusionModel)) {
    for (const auto& slot : statusVisuals_->confusions()) {
      const std::optional<StatusAnchor> anchor = slot ? statusAnchor(slot->fighter) : std::nullopt;
      if (!anchor) {
        continue;
      }
      for (std::int32_t object = 0; object < 3; ++object) {
        const auto angle = static_cast<float>(slot->angle + (object * ConfusionApart));
        const glm::vec3 offset{std::sin(sineRadiansOf(angle)) * ConfusionRadius, -ConfusionLift,
                               std::cos(sineRadiansOf(angle)) * ConfusionRadius};
        const std::int32_t facing = slot->offsets.at(static_cast<std::size_t>(object)) +
                                    slot->spin + ConfusionFacing + (object * ConfusionThird);
        submit(*model, anchor->condition + worldOf(offset), rotationYZ(facing, ConfusionTilt));
      }
    }
  }
  if (const EffectModelEntry* model = statusModel(StunRingModel)) {
    for (const auto& slot : statusVisuals_->stunRings()) {
      const std::optional<StatusAnchor> anchor = slot ? statusAnchor(slot->fighter) : std::nullopt;
      if (!anchor) {
        continue;
      }
      const auto frame = static_cast<float>(slot->frame);
      const float top = static_cast<float>(anchor->body.height) * SignLift / Percent;
      const float height = lerp(top, anchor->feet.y, StunRingFall, frame);
      float width = static_cast<float>(anchor->body.radius) * StunRingWidth / One;
      if (frame < StunRingGrowFrames) {
        width = lerp(width * StunRingFirstWidth, width, StunRingGrowFrames, frame);
      }
      const glm::mat4 scale = glm::scale(glm::mat4{1.0F}, {width, StunRingHeight, width});
      submit(*model, {anchor->feet.x, height, anchor->feet.z}, scale);
    }
  }
}

} // namespace dwe::app
