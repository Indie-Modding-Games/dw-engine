#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/image_palettes.hpp>
#include <dwe/assets/indexed_image.hpp>
#include <dwe/assets/map_images.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/core/log.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "field_scene.hpp"

namespace dwe::app {

namespace {

using game::battle::EffectBlend;
using game::battle::EffectMatrix;
using game::battle::EffectModel;
using game::battle::EffectSprite;
using game::battle::EffectStreak;
using game::battle::EffectVector;

// 4096 is 1 in the console's matrices; 128 is 1 in its colours.
constexpr float MatrixOne = 4096.0F;
constexpr float ColorOne = 128.0F;
// battle_effect.c: BTL_renderProjectedSprite scales the sprite by (scale * VIEWPORT_DISTANCE << 5)
// over the depth / 4, so a texel is scale / 32 world units wide whatever the depth.
constexpr float SpriteScaleUnit = 32.0F;
// GsSPRITE rotate: 4096 per degree, which the scripts give in degrees.
constexpr float DegreesPerHalfTurn = 180.0F;
// A line's colour bytes are its colour (drawLine2P: no texture to tint).
constexpr float ByteOne = 255.0F;
// The common effect sheet (ETCDAT/DEFEFF.TIM at 768, 384 of the console's VRAM) and the glow of
// effect_common.c: renderFXParticle on it: u 0, v 160 of the page at 768, 256, CLUT row 489.
constexpr std::string_view CommonSheet = "texture/etcdat/defeff";
constexpr std::int32_t GlowPalette = 1;
constexpr glm::vec2 GlowTexel{0.0F, 32.0F};
constexpr float GlowTexels = 15.0F;

// The world's axes are the console's with y and z the other way (core::fromOriginalAxes).
glm::vec3 worldOf(const EffectVector& v) {
  return {static_cast<float>(v.x), static_cast<float>(-v.y), static_cast<float>(-v.z)};
}

// A console transform's rotation and scale in the world's axes: F M F, F = diag(1, -1, -1).
glm::mat4 worldRotation(const EffectMatrix& m) {
  glm::mat4 out{1.0F};
  constexpr std::array<float, 3> Flip{1.0F, -1.0F, -1.0F};
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      // glm is column-major: out[column][row].
      out[static_cast<glm::length_t>(column)][static_cast<glm::length_t>(row)] =
          Flip.at(row) * Flip.at(column) * static_cast<float>(m.m.at(row).at(column)) / MatrixOne;
    }
  }
  return out;
}

assets::SpriteBlend spriteBlendOf(EffectBlend blend) {
  switch (blend) {
  case EffectBlend::Average:
    return assets::SpriteBlend::Average;
  case EffectBlend::Add:
    return assets::SpriteBlend::Add;
  case EffectBlend::Subtract:
    return assets::SpriteBlend::Subtract;
  case EffectBlend::AddQuarter:
    return assets::SpriteBlend::AddQuarter;
  case EffectBlend::Opaque:
    break;
  }
  return assets::SpriteBlend::Opaque;
}

// A paletted image of the content with a row of its CLUT (its _meta palettes), its colours with
// STP at SemiTransparentAlpha so a semi-transparent sprite mixes them (colorFromOriginal).
core::Result<assets::Image> sheetImage(const assets::VirtualFileSystem& files,
                                       const std::string& image, std::int32_t palette) {
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
  auto palettes = assets::parseImagePalettes(
      std::string_view{reinterpret_cast<const char*>(metaBytes->data()), metaBytes->size()});
  if (!palettes) {
    return std::unexpected{palettes.error()};
  }
  if (palette < 0 || std::cmp_greater_equal(palette, palettes->size())) {
    return core::fail(std::format("no palette {} of {}", palette, palettes->size()));
  }
  std::vector<assets::PaletteColor> colors;
  for (const std::uint16_t color : palettes->at(static_cast<std::size_t>(palette))) {
    colors.push_back(assets::colorFromOriginal(color));
  }
  return assets::applyPalette(*indexed, colors);
}

} // namespace

std::optional<std::size_t> FieldScene::effectTexture(const std::string& move,
                                                     std::int32_t palette) {
  return sheetTexture(std::format("effect/{}/texture", move), palette);
}

std::optional<std::size_t> FieldScene::sheetTexture(const std::string& name, std::int32_t palette) {
  const auto key = std::pair{name, palette};
  if (const auto found = effectTextures_.find(key); found != effectTextures_.end()) {
    return found->second;
  }
  std::optional<std::size_t> texture;
  auto image = sheetImage(*files_, name, palette);
  if (image) {
    if (auto added = mapRenderer_->addBillboardTexture(*image)) {
      texture = *added;
    } else {
      image = std::unexpected{added.error()};
    }
  }
  if (!image) {
    core::logWarning("battle: no effect texture {} (palette {}): {}", name, palette,
                     image.error().message);
  }
  effectTextures_.emplace(key, texture);
  return texture;
}

std::optional<std::size_t> FieldScene::plainTexture() {
  if (!plainTexture_) {
    constexpr auto White = std::byte{0xFF};
    const assets::Image white{.width = 1, .height = 1, .pixels = {White, White, White, White}};
    auto added = mapRenderer_->addBillboardTexture(white);
    if (!added) {
      core::logWarning("battle: no texture for the effects' lines: {}", added.error().message);
    }
    plainTexture_ = added ? std::optional{*added} : std::nullopt;
  }
  return *plainTexture_;
}

// battle_effect.c: BTL_renderParticleEmitters: a glowing particle near the camera is the common
// glow (effect_common.c: renderFXParticle: DEFEFF's 15 x 15 texels at 0, 32 with its second
// palette row, added); any other is a one-pixel line (drawLine2P). A line is a billboard on its
// middle, as long and as turned as the two ends fall on the background, one original pixel thick.
void FieldScene::addEffectStreak(const EffectStreak& streak,
                                 std::vector<render::MapBillboard>& billboards) {
  const render::MapCamera& camera = mapRenderer_->camera();
  const glm::vec3 from = worldOf(streak.from);
  const glm::vec3 to = worldOf(streak.to);
  const std::optional<render::BackgroundPoint> start = camera.projectToBackground(from);
  const std::optional<render::BackgroundPoint> end = camera.projectToBackground(to);
  if (!start || !end) {
    return;
  }
  const glm::vec3 color{static_cast<float>(streak.color.x), static_cast<float>(streak.color.y),
                        static_cast<float>(streak.color.z)};
  if (streak.glows && start->viewDepth < static_cast<float>(game::battle::StreakGlowDepth)) {
    const std::optional<std::size_t> glow = sheetTexture(std::string{CommonSheet}, GlowPalette);
    if (!glow) {
      return;
    }
    billboards.push_back({.centre = from,
                          .size = glm::vec2{static_cast<float>(game::battle::StreakGlowSize)},
                          .flat = false,
                          .texture = *glow,
                          .texel = GlowTexel,
                          .texels = glm::vec2{GlowTexels},
                          .blend = assets::SpriteBlend::Add,
                          .color = color / ColorOne});
    return;
  }
  const std::optional<std::size_t> plain = plainTexture();
  if (!plain) {
    return;
  }
  const glm::vec2 across = end->pixel - start->pixel;
  const float pixels = std::max(glm::length(across), 1.0F);
  const glm::vec3 middle = (from + to) * 0.5F;
  const float unitsPerPixel = camera.viewDepth(middle) / camera.desc().focalLength;
  billboards.push_back({.centre = middle,
                        .size = glm::vec2{pixels, 1.0F} * unitsPerPixel,
                        .flat = false,
                        .texture = *plain,
                        .texel = glm::vec2{0.0F},
                        .texels = glm::vec2{1.0F},
                        .blend = assets::SpriteBlend::Opaque,
                        .color = color / ByteOne,
                        .turn = std::atan2(across.y, across.x)});
}

const FieldScene::EffectModelEntry* FieldScene::effectModel(const std::string& move) {
  if (const auto found = effectModels_.find(move); found != effectModels_.end()) {
    return found->second ? &*found->second : nullptr;
  }
  std::optional<EffectModelEntry> entry;
  auto id = assets::AssetId::parse(std::format("effect/{}/model", move));
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
    core::logWarning("battle: no model for the effect of {}: {}", move, data.error().message);
  }
  auto [at, inserted] = effectModels_.emplace(move, entry);
  return at->second ? &*at->second : nullptr;
}

void FieldScene::addEffectBillboards(std::vector<render::MapBillboard>& billboards) {
  for (const SceneEffect& effect : effects_) {
    if (const auto* streak = std::get_if<EffectStreak>(&effect.shape)) {
      addEffectStreak(*streak, billboards);
      continue;
    }
    const auto* sprite = std::get_if<EffectSprite>(&effect.shape);
    if (sprite == nullptr || sprite->width <= 0 || sprite->height <= 0) {
      continue;
    }
    const std::optional<std::size_t> texture = effectTexture(effect.move, sprite->clutRow);
    if (!texture) {
      continue;
    }
    const glm::vec2 texels{static_cast<float>(sprite->width), static_cast<float>(sprite->height)};
    billboards.push_back({.centre = worldOf(sprite->position),
                          .size = {texels.x * static_cast<float>(sprite->scaleX) / SpriteScaleUnit,
                                   texels.y * static_cast<float>(sprite->scaleY) / SpriteScaleUnit},
                          .flat = false,
                          .texture = *texture,
                          .texel = {static_cast<float>(sprite->u), static_cast<float>(sprite->v)},
                          .texels = texels,
                          .blend = spriteBlendOf(sprite->blend),
                          .color = glm::vec3{static_cast<float>(sprite->color.x),
                                             static_cast<float>(sprite->color.y),
                                             static_cast<float>(sprite->color.z)} /
                                   ColorOne,
                          .pivot = glm::vec2{static_cast<float>(sprite->pivotX),
                                             static_cast<float>(sprite->pivotY)} /
                                   texels,
                          .turn = static_cast<float>(sprite->rotate) * std::numbers::pi_v<float> /
                                  DegreesPerHalfTurn});
  }
}

// battle_effect.c: BTL_drawTMD: one object of the effect's model with the instance's transform.
// Each object is a joint of the model's skin at the origin: the drawn one takes the transform, the
// others fold to nothing.
void FieldScene::submitEffectModels() {
  std::vector<glm::mat4> joints;
  for (const SceneEffect& effect : effects_) {
    const auto* model = std::get_if<EffectModel>(&effect.shape);
    if (model == nullptr) {
      continue;
    }
    const EffectModelEntry* entry = effectModel(effect.move);
    if (entry == nullptr || model->object < 0 ||
        static_cast<std::size_t>(model->object) >= entry->joints) {
      continue;
    }
    joints.assign(entry->joints, glm::mat4{0.0F});
    joints[static_cast<std::size_t>(model->object)] = worldRotation(model->transform);
    const glm::vec3 color = model->color ? glm::vec3{static_cast<float>(model->color->x),
                                                     static_cast<float>(model->color->y),
                                                     static_cast<float>(model->color->z)} /
                                               ColorOne
                                         : glm::vec3{1.0F};
    models_->submit({.model = entry->handle,
                     .world = glm::translate(glm::mat4{1.0F}, worldOf(model->transform.t)),
                     .joints = joints,
                     .tint = glm::vec4{color, 1.0F}});
  }
}

std::optional<std::span<const assets::NodeTransform>>
FieldScene::fighterPose(std::optional<std::int32_t> npc) const {
  const Actor* actor = fighterActor(npc);
  if (actor == nullptr) {
    return std::nullopt;
  }
  return actor->player->pose();
}

} // namespace dwe::app
