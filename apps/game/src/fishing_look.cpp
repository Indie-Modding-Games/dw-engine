#include "fishing_look.hpp"

#include <cmath>
#include <cstddef>
#include <format>
#include <string_view>

#include "body_face.hpp"
#include "evolution_effects.hpp"

namespace dwe::app {

namespace {

using game::fishing::FishingSprite;

// FISH_updateRodPose: the player's node that holds the rod.
constexpr std::uint32_t RodHandNode = 9;
// FISH_loadFishing: ETCDAT/SAO.TMD, the rod's four pieces. The amazing rod moves the eight rows of
// its texels below over the old rod's (MoveImage from y 0x1e4 to 0x1dc).
constexpr std::string_view RodModel = "model/etcdat/sao";
constexpr float AmazingRodRows = 8.0F;
// FISH_drawSprite3D's colour: 0x80, the texels as they are.
constexpr float FullShade = 128.0F;

// One sprite of the map's image at its corners, on its own palette.
void addSprite(FieldScene& scene, const ScenePrimFrame& frame, std::string_view image,
               const FishingSprite& sprite, const std::array<glm::vec3, 4>& corners,
               glm::vec4 color, ScenePrimBlend blend, std::vector<ScenePrim>& prims) {
  const std::optional<rhi::TextureId> texture =
      scene.sceneImage(std::format("{}?palette={}", image, sprite.palette));
  if (!texture) {
    return;
  }
  const std::array<glm::vec2, 4> uvs = game::fishing::spriteUvs(sprite);
  ScenePrim prim{
      .shape = ScenePrim::Shape::Quad, .color = color, .texture = *texture, .blend = blend};
  for (std::size_t corner = 0; corner < corners.size(); ++corner) {
    prim.corners.at(corner).clip = frame.clip(corners.at(corner));
    prim.corners.at(corner).uv = uvs.at(corner);
  }
  prims.push_back(prim);
}

// FISH_drawFishSprite: flat on the water, its head (the sprite's top) where it swims, drawn
// subtractive by its shade.
void addFish(FieldScene& scene, const ScenePrimFrame& frame, const game::fishing::Fishing& water,
             const game::fishing::Fish& fish, bool struggling, std::vector<ScenePrim>& prims) {
  const game::fishing::FishingPlace& place = water.place();
  const std::optional<game::fishing::FishLook> look = game::fishing::fishLook(
      fish, struggling, {.small = place.fishScaleSmall, .large = place.fishScaleLarge});
  if (!look || look->shade <= 0) {
    return;
  }
  const glm::vec3 ahead{std::sin(fish.yaw), 0.0F, std::cos(fish.yaw)};
  const glm::vec3 right{-ahead.z, 0.0F, ahead.x};
  const FishingSprite& sprite = game::fishing::fishingSprite(look->sprite);
  const float shade = static_cast<float>(look->shade) / FullShade;
  addSprite(scene, frame, place.spriteImage, sprite,
            game::fishing::spriteCorners(sprite, fish.position, right, -ahead, look->scale),
            glm::vec4{glm::vec3{shade}, 1.0F}, ScenePrimBlend::Subtractive, prims);
}

// FISH_drawRodSegments: the model's four pieces at the first four joints, lit as the field's
// models.
void addRod(FieldScene& scene, const ScenePrimFrame& frame, const game::fishing::RodPose& pose,
            game::fishing::Rod rod, std::vector<ScenePrim>& prims) {
  const std::vector<std::vector<BodyFace>>* pieces = scene.sceneModelFaces(RodModel);
  if (pieces == nullptr) {
    return;
  }
  const float rows =
      rod == game::fishing::Rod::Amazing ? AmazingRodRows / game::fishing::SpriteImageSize.y : 0.0F;
  std::vector<SortedPrim> faces;
  for (std::size_t piece = 0; piece < game::fishing::RodObjects && piece < pieces->size();
       ++piece) {
    const glm::mat4& place = pose.joints.at(piece);
    const glm::mat3 turn{place};
    for (const BodyFace& face : pieces->at(piece)) {
      ScenePrim prim{.shape = face.count == 4 ? ScenePrim::Shape::Quad : ScenePrim::Shape::Triangle,
                     .color = glm::vec4{scene.modelLight(turn * face.normal), 1.0F},
                     .texture = face.texture};
      float depth = 0.0F;
      for (std::size_t corner = 0; corner < face.count; ++corner) {
        const glm::vec3 world{place * glm::vec4{face.corners.at(corner), 1.0F}};
        prim.corners.at(corner).clip = frame.clip(world);
        prim.corners.at(corner).uv = face.uvs.at(corner) + glm::vec2{0.0F, rows};
        depth += frame.depth(world);
      }
      faces.push_back({.depth = depth / static_cast<float>(face.count), .prim = prim});
    }
  }
  for (ScenePrim& prim : sortedPrims(std::move(faces))) {
    prims.push_back(prim);
  }
}

} // namespace

std::optional<game::fishing::RodPose> fishingRodPose(const FieldScene& scene,
                                                     const game::fishing::Fishing& water) {
  const std::optional<glm::mat4> hand = scene.playerNodeTransform(RodHandNode);
  if (!hand) {
    return std::nullopt;
  }
  return game::fishing::rodPose(*hand, water.bobber(), game::fishing::rodBend(water.tension()));
}

void addFishingFish(FieldScene& scene, const game::fishing::Fishing& water,
                    const ScenePrimFrame& frame, std::vector<ScenePrim>& prims) {
  if (water.disabled()) {
    return;
  }
  for (const game::fishing::Fish& fish : water.fish()) {
    addFish(scene, frame, water, fish, false, prims);
  }
  if (const std::optional<game::fishing::Fish>& hooked = water.hooked()) {
    addFish(scene, frame, water, *hooked, game::fishing::hookedStruggles(*hooked), prims);
  }
}

void addFishingRod(FieldScene& scene, const game::fishing::Fishing& water,
                   const ScenePrimFrame& frame, std::vector<ScenePrim>& prims) {
  // The rod and the bobber show from the moment the player holds the rod out.
  if (water.disabled() || !water.fishing() || !water.bobberShown()) {
    return;
  }
  if (const std::optional<game::fishing::RodPose> pose = fishingRodPose(scene, water)) {
    addRod(scene, frame, *pose, water.rod(), prims);
  }
  if (const std::optional<std::size_t> sprite =
          game::fishing::bobberSprite(water.bobberFrame(), water.rod())) {
    // Upright, across the world's x axis.
    const FishingSprite& look = game::fishing::fishingSprite(*sprite);
    addSprite(scene, frame, water.place().spriteImage, look,
              game::fishing::spriteCorners(look, water.bobber(), glm::vec3{1.0F, 0.0F, 0.0F},
                                           glm::vec3{0.0F, -1.0F, 0.0F}, water.place().spriteScale),
              glm::vec4{1.0F}, ScenePrimBlend::Alpha, prims);
  }
}

} // namespace dwe::app
