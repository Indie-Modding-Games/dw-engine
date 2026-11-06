#pragma once

#include <dwe/core/math.hpp>
#include <dwe/game/fishing/fishing.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// How the fishing looks (fish.c: FISH_renderFishingWorld): the rod's model bent toward the bobber,
// the bobber's and the fish's sprites from the map's sprite image, placed in the world. Pure
// functions: the caller draws the quads and the model's faces.
namespace dwe::game::fishing {

// One sprite of the fishing's image (FISH_SPRITES): its texels from (u0, v0) to (u1, v1) in the
// texture page (a sprite with u1 < u0 is mirrored), the texel it hangs from, and the image's
// palette.
struct FishingSprite {
  std::int32_t u0 = 0;
  std::int32_t v0 = 0;
  std::int32_t u1 = 0;
  std::int32_t v1 = 0;
  std::int32_t originX = 0;
  std::int32_t originY = 0;
  std::int32_t palette = 0;
};

// The sprite image starts this many rows down its texture page (ETCDAT/FISHING.TIM and
// FISHING2.TIM sit at y 396 of the video memory, the page at y 256).
inline constexpr std::int32_t SpriteImageTop = 0x8C;
inline constexpr glm::vec2 SpriteImageSize{256.0F, 116.0F};
inline constexpr std::size_t FishingSpriteCount = 49;
[[nodiscard]] const FishingSprite& fishingSprite(std::size_t index);
// A sprite's texture coordinates in the sprite image (0 to 1), in spriteCorners' order.
[[nodiscard]] std::array<glm::vec2, 4> spriteUvs(const FishingSprite& sprite);

// The four corners of a sprite drawn by FISH_drawSprite3D, scaled by `scale` (4096 for one texel
// a unit): from its origin, `right` along its texels' rows and `down` along their columns. In the
// original's corner order: top left, top right, bottom left, bottom right.
[[nodiscard]] std::array<glm::vec3, 4> spriteCorners(const FishingSprite& sprite, glm::vec3 origin,
                                                     glm::vec3 right, glm::vec3 down,
                                                     std::int32_t scale);

// FISH_bobberCommand, command 1: the bobber's sprite for its frame (1 to 7) and the rod, nothing
// for frame 0 or below (hidden).
[[nodiscard]] std::optional<std::size_t> bobberSprite(std::int32_t frame, Rod rod);

// FISH_drawFishSprite: a fish's sprite, its scale (4096 for one texel a unit) and its shade (the
// texels' share it takes from what is behind it, subtractive, out of 128). A struggling fish (on
// the hook, past pulling the bobber) has its own frames. The sea serpent is a model.
struct FishLook {
  std::size_t sprite = 0;
  std::int32_t scale = 0;
  std::int32_t shade = 0;
};
// The map's fish scales (FISHING_MAP_DATA: fishScaleSmall, fishScaleLarge), per size unit.
struct FishScales {
  std::int32_t small = 0;
  std::int32_t large = 0;
};
[[nodiscard]] std::optional<FishLook> fishLook(const Fish& fish, bool struggling,
                                               FishScales scales);
// Whether the hooked fish draws its struggling frames: past nibbling and pulling the bobber.
[[nodiscard]] bool hookedStruggles(const Fish& fish);

// FISH_updateRodPose: the rod's five joints, from the player's bone 9 (the hand that holds it),
// the last four bent toward the bobber as far as the line's tension lets them (rodBend). Each
// segment's transform takes the rod model's object of the same number (the first four) to the
// world; the fifth joint is the tip, where the line starts.
inline constexpr std::size_t RodJoints = 5;
inline constexpr std::size_t RodObjects = 4;
struct RodPose {
  std::array<glm::mat4, RodJoints> joints{};
  glm::vec3 tip{0.0F};
};
// How far the rod bends (in 4096ths of a turn): 128 at rest, 0 to 512 as the tension goes from
// slack to snapping.
[[nodiscard]] std::int32_t rodBend(const Tension& tension);
// `hand` is the bone's transform in the world (y up, as the rest of the fishing), `bobber` the
// bobber's position.
[[nodiscard]] RodPose rodPose(const glm::mat4& hand, glm::vec3 bobber, std::int32_t bend);

} // namespace dwe::game::fishing
