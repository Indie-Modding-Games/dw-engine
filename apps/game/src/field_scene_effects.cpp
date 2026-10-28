#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/image_palettes.hpp>
#include <dwe/assets/indexed_image.hpp>
#include <dwe/assets/map_images.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/core/log.hpp>

#include <algorithm>
#include <array>
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
using game::battle::EffectVector;

// 4096 is 1 in the console's matrices; 128 is 1 in its colours.
constexpr float MatrixOne = 4096.0F;
constexpr float ColorOne = 128.0F;
// battle_effect.c: BTL_renderProjectedSprite scales the sprite by (scale * VIEWPORT_DISTANCE << 5)
// over the depth / 4, so a texel is scale / 32 world units wide whatever the depth.
constexpr float SpriteScaleUnit = 32.0F;
// GsSPRITE rotate: 4096 per degree, which the scripts give in degrees.
constexpr float DegreesPerHalfTurn = 180.0F;

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

// The effect's texture with a row of its CLUT (texture_meta palettes), its colours with STP at
// SemiTransparentAlpha so a semi-transparent sprite mixes them (colorFromOriginal).
core::Result<assets::Image> effectImage(const assets::VirtualFileSystem& files,
                                        const std::string& move, std::int32_t palette) {
  auto png = assets::AssetId::parse(std::format("effect/{}/texture", move));
  auto meta =
      assets::AssetId::parse(std::format("effect/{}/texture{}", move, assets::ImageMetaSuffix));
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
  const auto key = std::pair{move, palette};
  if (const auto found = effectTextures_.find(key); found != effectTextures_.end()) {
    return found->second;
  }
  std::optional<std::size_t> texture;
  auto image = effectImage(*files_, move, palette);
  if (image) {
    if (auto added = mapRenderer_->addBillboardTexture(*image)) {
      texture = *added;
    } else {
      image = std::unexpected{added.error()};
    }
  }
  if (!image) {
    core::logWarning("battle: no texture for the effect of {} (palette {}): {}", move, palette,
                     image.error().message);
  }
  effectTextures_.emplace(key, texture);
  return texture;
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

std::optional<glm::mat4> FieldScene::fighterNode(std::optional<std::int32_t> npc,
                                                 std::uint32_t node) const {
  const Actor* actor = fighterActor(npc);
  if (actor == nullptr || !actor->model->skin) {
    return std::nullopt;
  }
  const auto joints = actor->player->jointMatrices();
  const auto& skin = *actor->model->skin;
  const std::size_t count = std::min(joints.size(), skin.inverseBindMatrices.size());
  if (count == 0) {
    return std::nullopt;
  }
  // A node the skin lacks stands for the model's first (the effects' bone lists name the
  // original's nodes).
  const std::size_t joint = node < count ? node : 0;
  const glm::mat4 jointSpace = joints[joint] * glm::inverse(skin.inverseBindMatrices[joint]);
  const auto& transform = world_.registry().get<scene::Transform>(actor->entity);
  const glm::mat4 world = glm::rotate(glm::translate(glm::mat4{1.0F}, transform.position),
                                      transform.yaw, glm::vec3{0.0F, 1.0F, 0.0F});
  return world * jointSpace;
}

} // namespace dwe::app
