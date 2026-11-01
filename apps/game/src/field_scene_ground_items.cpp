#include <dwe/assets/map_images.hpp>
#include <dwe/core/log.hpp>
#include <dwe/ui/image_reference.hpp>

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "field_scene.hpp"

namespace dwe::app {

namespace {

// The items' icons: ETCDAT/ITEM.TIM, a 16 x 16 cell per item number in rows of 16, drawn with the
// item's palette (utils2.c: setItemTexture). The last column and row are 15 texels.
constexpr std::string_view ItemImage = "texture/etcdat/item";
constexpr std::int32_t IconSide = 16;
constexpr std::int32_t IconsPerRow = 16;
constexpr std::int32_t LastIconTexel = 0xF0;

// item.c: renderOverworldItem: a square facing the camera, centred on the item, of
// (VIEWPORT_DISTANCE << 7) / (otz * 4) pixels where otz is the depth / 4: 128 units in the world.
constexpr float IconWorldSide = 128.0F;

// item.c: renderDroppedItemShadow: a 200 x 200 quad on the ground (y 0) under the item, from
// ETCDAT/SPOT256.TIM (tpage 832, 256; uv 0x40, 0x80, 63 x 63), semi-transparent with mode 2
// (subtract) and colour 0x30, behind everything (ordering table 0xFFD).
constexpr std::string_view ShadowImage = "texture/etcdat/spot256";
constexpr float ShadowWorldSide = 200.0F;
constexpr glm::vec2 ShadowTexel{64.0F, 0.0F};
constexpr glm::vec2 ShadowTexels{63.0F, 63.0F};
constexpr float ShadowShade = 0x30 / 128.0F;

// The image's pixels for a billboard: the items' image with a palette, or the shadow's. The
// shadow's prim is semi-transparent and every colour of its palette has STP, so its drawn texels
// all mix.
core::Result<assets::Image> groundItemImage(const assets::VirtualFileSystem& files,
                                            std::optional<std::uint8_t> palette) {
  const std::string source =
      palette ? std::format("{}?palette={}", ItemImage, *palette) : std::string{ShadowImage};
  auto reference = ui::parseImageReference(source);
  if (!reference) {
    return std::unexpected{reference.error()};
  }
  auto image = ui::loadImage(files, *reference);
  if (!image || palette) {
    return image;
  }
  constexpr std::size_t Channels = 4;
  for (std::size_t alpha = Channels - 1; alpha < image->pixels.size(); alpha += Channels) {
    if (image->pixels[alpha] != std::byte{0}) {
      image->pixels[alpha] = std::byte{assets::SemiTransparentAlpha};
    }
  }
  return image;
}

} // namespace

std::optional<std::size_t> FieldScene::groundItemTexture(std::optional<std::uint8_t> palette) {
  if (const auto found = groundItemTextures_.find(palette); found != groundItemTextures_.end()) {
    return found->second;
  }
  std::optional<std::size_t> texture;
  auto image = groundItemImage(*files_, palette);
  if (image) {
    if (auto added = mapRenderer_->addBillboardTexture(*image)) {
      texture = *added;
    } else {
      image = std::unexpected{added.error()};
    }
  }
  if (!image) {
    core::logWarning("field: no ground item image for palette {}: {}",
                     palette ? std::to_string(*palette) : std::string{"of the shadow"},
                     image.error().message);
  }
  groundItemTextures_.emplace(palette, texture);
  return texture;
}

void FieldScene::drawGroundItems() {
  std::vector<render::MapBillboard> billboards;
  // The shadows first: the original draws them behind everything.
  for (const GroundItemSprite& item : groundItems_) {
    if (const std::optional<std::size_t> shadow = groundItemTexture(std::nullopt)) {
      billboards.push_back({.centre = {item.position.x, 0.0F, item.position.z},
                            .size = glm::vec2{ShadowWorldSide},
                            .flat = true,
                            .texture = *shadow,
                            .texel = ShadowTexel,
                            .texels = ShadowTexels,
                            .blend = assets::SpriteBlend::Subtract,
                            .color = glm::vec3{ShadowShade}});
    }
  }
  const auto addIcon = [&](glm::vec3 centre, std::int32_t number, std::uint8_t palette) {
    const std::optional<std::size_t> icon = groundItemTexture(palette);
    if (!icon) {
      return;
    }
    const std::int32_t u = (number % IconsPerRow) * IconSide;
    const std::int32_t v = (number / IconsPerRow) * IconSide;
    const glm::vec2 texels{u == LastIconTexel ? IconSide - 1 : IconSide,
                           v == LastIconTexel ? IconSide - 1 : IconSide};
    billboards.push_back({.centre = centre,
                          .size = glm::vec2{IconWorldSide},
                          .flat = false,
                          .texture = *icon,
                          .texel = {static_cast<float>(u), static_cast<float>(v)},
                          .texels = texels,
                          .blend = assets::SpriteBlend::Opaque,
                          .color = glm::vec3{1.0F}});
  };
  for (const GroundItemSprite& item : groundItems_) {
    addIcon(item.position, item.icon, item.palette);
  }
  // game_menu.c: renderFeedingItem: the item at the player's node, through renderOverworldItem.
  if (heldItem_) {
    if (const std::optional<glm::vec3> hand = playerNodePoint(heldItem_->node, glm::vec3{0.0F})) {
      addIcon(*hand, heldItem_->icon, heldItem_->palette);
    }
  }
  addEffectBillboards(billboards);
  addStatusBillboards(billboards);
  addBattleVisualBillboards(billboards);
  addFlatFighterBillboards(billboards);
  mapRenderer_->setBillboards(billboards);
}

} // namespace dwe::app
