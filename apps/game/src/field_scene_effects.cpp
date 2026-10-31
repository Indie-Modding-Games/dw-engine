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
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "field_scene.hpp"

namespace dwe::app {

namespace {

using game::battle::EffectBlend;
using game::battle::EffectCloud;
using game::battle::EffectFlash;
using game::battle::EffectMatrix;
using game::battle::EffectModel;
using game::battle::EffectPaletteRow;
using game::battle::EffectPalettes;
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
// The effects' textures are 4-bit (their metadata's mode indexed4).
constexpr std::int32_t EffectTextureBits = 4;
constexpr std::size_t RgbaChannels = 4;
constexpr auto OpaqueAlpha = std::byte{0xFF};
// The common effect sheet (ETCDAT/DEFEFF.TIM at 768, 384 of the console's VRAM) and the glow of
// effect_common.c: renderFXParticle on it: u 0, v 160 of the page at 768, 256, CLUT row 489.
constexpr std::string_view CommonSheet = "texture/etcdat/defeff";
constexpr std::int32_t GlowPalette = 1;
constexpr glm::vec2 GlowTexel{0.0F, 32.0F};
constexpr float GlowTexels = 15.0F;
// efe.c: CLOUD_FX_SPRITE: 32 x 32 texels of the same page at v 128 (the sheet's top row), CLUT row
// 488, centred; renderSprite scales it by scale * VIEWPORT_DISTANCE / depth, so it is 32 * scale /
// 4096 world units wide whatever the depth.
constexpr std::int32_t CloudPalette = 0;
constexpr float CloudTexels = 32.0F;
// efe.c: renderEFEFlash: SPOT256 (8-bit, at 832, 384, CLUT row 487) in the page at 832, 256; its
// 64 x 64 quarter at u and v 0xC0 of the page (row 64 of the sheet), corner at the centre.
constexpr std::string_view LightSheet = "texture/etcdat/spot256";
constexpr std::int32_t FlashQuarter = 64;
constexpr std::int32_t FlashRow = 64;
constexpr std::size_t PixelBytes = 4;
// renderParticleFlash: half side (scale * 63 >> 10) + 1 pixels, with scale = focal * size * 10 /
// depth + swell; nothing from 0x8000 on, nor sorted at 0x20 or less or from 0x1000 on.
constexpr std::int32_t FlashSizeScale = 10;
constexpr std::int32_t FlashScaleShift = 10;
constexpr std::int32_t FlashScaleLimit = 0x8000;
constexpr std::int32_t FlashNearestOrder = 0x20;
constexpr std::int32_t FlashFarOrder = 0x1000;
constexpr std::int32_t OrderShift = 4;

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
                                       const std::string& image, std::int32_t palette,
                                       const EffectPaletteRow* rewritten) {
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
    const std::size_t index = colors.size();
    const bool written = rewritten != nullptr && index < rewritten->colors.size() &&
                         rewritten->colors.at(index).has_value();
    colors.push_back(assets::colorFromOriginal(written ? *rewritten->colors.at(index) : color));
  }
  return assets::applyPalette(*indexed, colors);
}

} // namespace

core::Result<assets::Image> effectSheetImage(const assets::VirtualFileSystem& files,
                                             const std::string& image, std::int32_t palette,
                                             const EffectPaletteRow* rewritten) {
  return sheetImage(files, image, palette, rewritten);
}

const EffectPaletteRow* rewrittenRow(const EffectPalettes& palettes, std::int32_t palette) {
  const auto found = palettes.find(palette);
  return found != palettes.end() ? &found->second : nullptr;
}

std::optional<std::size_t> FieldScene::effectTexture(const std::string& move, std::int32_t palette,
                                                     const EffectPalettes& palettes) {
  return sheetTexture(std::format("effect/{}/texture", move), palette,
                      rewrittenRow(palettes, palette));
}

std::optional<std::size_t> FieldScene::sheetTexture(const std::string& name, std::int32_t palette,
                                                    const EffectPaletteRow* rewritten) {
  const auto key =
      std::tuple{name, palette, rewritten != nullptr ? std::optional{*rewritten} : std::nullopt};
  if (const auto found = effectTextures_.find(key); found != effectTextures_.end()) {
    return found->second;
  }
  std::optional<std::size_t> texture;
  auto image = sheetImage(*files_, name, palette, rewritten);
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

// efe.c: renderCloudFX: a puff of the common sheet, added, its grey shade the tint.
void FieldScene::addEffectCloud(const EffectCloud& cloud,
                                std::vector<render::MapBillboard>& billboards) {
  const std::optional<std::size_t> texture = sheetTexture(std::string{CommonSheet}, CloudPalette);
  if (!texture) {
    return;
  }
  const float side = CloudTexels * static_cast<float>(cloud.scale) / MatrixOne;
  billboards.push_back({.centre = worldOf(cloud.position),
                        .size = glm::vec2{side},
                        .flat = false,
                        .texture = *texture,
                        .texel = glm::vec2{static_cast<float>(cloud.u), 0.0F},
                        .texels = glm::vec2{CloudTexels},
                        .blend = assets::SpriteBlend::Add,
                        .color = glm::vec3{static_cast<float>(cloud.shade)} / ColorOne});
}

std::optional<std::size_t> FieldScene::flashTexture(std::int32_t u) {
  if (const auto found = flashTextures_.find(u); found != flashTextures_.end()) {
    return found->second;
  }
  std::optional<std::size_t> texture;
  auto sheet = sheetImage(*files_, std::string{LightSheet}, 0, nullptr);
  if (sheet && (std::cmp_less(sheet->width, u + FlashQuarter) ||
                std::cmp_less(sheet->height, FlashRow + FlashQuarter))) {
    sheet = core::fail(std::format("{} x {} texels, too small", sheet->width, sheet->height));
  }
  if (sheet) {
    // renderParticleFlash draws the quarter four times, mirrored so its corner meets the centre.
    constexpr std::int32_t Side = 2 * FlashQuarter;
    assets::Image glow{.width = Side, .height = Side, .pixels = {}};
    glow.pixels.resize(static_cast<std::size_t>(Side) * Side * PixelBytes);
    for (std::int32_t y = 0; y < Side; ++y) {
      for (std::int32_t x = 0; x < Side; ++x) {
        const std::int32_t qx = x < FlashQuarter ? x : Side - 1 - x;
        const std::int32_t qy = y < FlashQuarter ? y : Side - 1 - y;
        const auto from = ((static_cast<std::size_t>(FlashRow + qy) * sheet->width) +
                           static_cast<std::size_t>(u + qx)) *
                          PixelBytes;
        const auto to =
            ((static_cast<std::size_t>(y) * Side) + static_cast<std::size_t>(x)) * PixelBytes;
        std::copy_n(sheet->pixels.begin() + static_cast<std::ptrdiff_t>(from), PixelBytes,
                    glow.pixels.begin() + static_cast<std::ptrdiff_t>(to));
      }
    }
    if (auto added = mapRenderer_->addBillboardTexture(glow)) {
      texture = *added;
    } else {
      sheet = std::unexpected{added.error()};
    }
  }
  if (!sheet) {
    core::logWarning("battle: no texture for the effects' flashes: {}", sheet.error().message);
  }
  flashTextures_.emplace(u, texture);
  return texture;
}

// efe.c: renderEFEFlash, renderParticleFlash: the glow's size on the screen comes from the true
// depth; a fixed depth only sorts it (in front of nearly everything), so the billboard moves along
// its view ray to that depth, scaled to keep its size on the screen.
void FieldScene::addEffectFlash(const EffectFlash& flash,
                                std::vector<render::MapBillboard>& billboards) {
  const render::MapCamera& camera = mapRenderer_->camera();
  const glm::vec3 at = worldOf(flash.position);
  const float depth = camera.viewDepth(at);
  if (depth < 1.0F) {
    return;
  }
  const auto originalDepth = static_cast<std::int64_t>(depth);
  const std::int64_t scale = (static_cast<std::int64_t>(camera.desc().focalLength) * flash.size *
                              FlashSizeScale / originalDepth) +
                             flash.bump;
  const std::int64_t order =
      flash.fixedDepth > 0 ? flash.fixedDepth : (originalDepth >> OrderShift);
  if (scale >= FlashScaleLimit || order <= FlashNearestOrder || order >= FlashFarOrder) {
    return;
  }
  const std::int64_t half = ((scale * (FlashQuarter - 1)) >> FlashScaleShift) + 1;
  if (half <= 1) {
    return;
  }
  const std::optional<std::size_t> texture = flashTexture(flash.u);
  if (!texture) {
    return;
  }
  const float placed = flash.fixedDepth > 0 ? static_cast<float>(order << OrderShift) : depth;
  const glm::vec3 eye{glm::inverse(camera.view())[3]};
  const glm::vec3 centre = eye + ((at - eye) * (placed / depth));
  const float side = 2.0F * static_cast<float>(half) * placed / camera.desc().focalLength;
  billboards.push_back(
      {.centre = centre,
       .size = glm::vec2{side},
       .flat = false,
       .texture = *texture,
       .texel = glm::vec2{0.0F},
       .texels = glm::vec2{static_cast<float>(2 * FlashQuarter)},
       .blend = spriteBlendOf(flash.blend),
       .color = glm::vec3{static_cast<float>(flash.color.x), static_cast<float>(flash.color.y),
                          static_cast<float>(flash.color.z)} /
                ColorOne});
}

// The effect's model with the palette rows its scripts rewrote: a material on the effect's
// texture with a rewritten row of the texture's CLUT takes the texture in that row's colours, as
// the console draws it from VRAM. Only the rows its materials read make another model.
const FieldScene::EffectModelEntry* FieldScene::effectModel(const std::string& move,
                                                            const EffectPalettes& palettes) {
  EffectPalettes read;
  if (!palettes.empty()) {
    effectModel(move, {});
    for (const std::int32_t row : effectModelRows_[move]) {
      if (const auto found = palettes.find(row); found != palettes.end()) {
        read.insert(*found);
      }
    }
  }
  const auto key = std::pair{move, read};
  if (const auto found = effectModels_.find(key); found != effectModels_.end()) {
    return found->second ? &*found->second : nullptr;
  }
  std::optional<EffectModelEntry> entry;
  auto id = assets::AssetId::parse(std::format("effect/{}/model", move));
  auto data = id ? assets::loadModel(*files_, *id)
                 : core::Result<assets::ModelData>{std::unexpected{id.error()}};
  if (data) {
    std::set<std::int32_t> rows = repaintEffectModel(move, read, *data);
    if (read.empty()) {
      effectModelRows_[move] = std::move(rows);
    }
  }
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
  auto [at, inserted] = effectModels_.emplace(key, entry);
  return at->second ? &*at->second : nullptr;
}

// Repaints the materials of the model on the effect's texture whose CLUT row `palettes` rewrote;
// returns the rows all of them read.
std::set<std::int32_t> FieldScene::repaintEffectModel(const std::string& move,
                                                      const EffectPalettes& palettes,
                                                      assets::ModelData& data) const {
  std::set<std::int32_t> rows;
  const std::string name = std::format("effect/{}/texture", move);
  auto meta = assets::AssetId::parse(std::format("{}{}", name, assets::ImageMetaSuffix));
  auto metaBytes = meta ? files_->read(*meta)
                        : core::Result<std::vector<std::byte>>{std::unexpected{meta.error()}};
  auto page = metaBytes ? assets::parseMapTexturePage(
                              EffectTextureBits, {},
                              std::string_view{reinterpret_cast<const char*>(metaBytes->data()),
                                               metaBytes->size()})
                        : core::Result<assets::MapTexturePage>{std::unexpected{metaBytes.error()}};
  if (!page) {
    core::logDebug("battle: no place of the effect texture of {}: {}", move, page.error().message);
    return rows;
  }
  for (const assets::ModelMaterial& material : data.materials) {
    const assets::VramPoint at{.x = static_cast<std::uint32_t>(page->vramX),
                               .y = static_cast<std::uint32_t>(page->vramY)};
    if (!material.image || !material.texture || !material.clut || *material.texture != at ||
        *material.image >= data.images.size()) {
      continue;
    }
    const auto row = static_cast<std::int32_t>(material.clut->y) - page->clutRow;
    rows.insert(row);
    const auto found = palettes.find(row);
    if (found == palettes.end()) {
      continue;
    }
    auto image = sheetImage(*files_, name, row, &found->second);
    assets::Image& target = data.images[*material.image];
    if (!image || image->width != target.width || image->height != target.height) {
      continue;
    }
    // The model's own images keep a colour with STP opaque: its material mixes it.
    for (std::size_t alpha = RgbaChannels - 1; alpha < image->pixels.size();
         alpha += RgbaChannels) {
      if (image->pixels[alpha] == std::byte{assets::SemiTransparentAlpha}) {
        image->pixels[alpha] = OpaqueAlpha;
      }
    }
    target = std::move(*image);
  }
  return rows;
}

void FieldScene::addEffectBillboards(std::vector<render::MapBillboard>& billboards) {
  for (const SceneEffect& effect : effects_) {
    if (const auto* streak = std::get_if<EffectStreak>(&effect.shape)) {
      addEffectStreak(*streak, billboards);
      continue;
    }
    if (const auto* cloud = std::get_if<EffectCloud>(&effect.shape)) {
      addEffectCloud(*cloud, billboards);
      continue;
    }
    if (const auto* flash = std::get_if<EffectFlash>(&effect.shape)) {
      addEffectFlash(*flash, billboards);
      continue;
    }
    const auto* sprite = std::get_if<EffectSprite>(&effect.shape);
    if (sprite == nullptr || sprite->width <= 0 || sprite->height <= 0) {
      continue;
    }
    const std::optional<std::size_t> texture =
        effectTexture(effect.move, sprite->clutRow, effect.palettes);
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
    const EffectModelEntry* entry = effectModel(effect.move, effect.palettes);
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
