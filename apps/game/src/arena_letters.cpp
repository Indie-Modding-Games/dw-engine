#include "arena_letters.hpp"

#include <dwe/assets/asset_id.hpp>

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>
#include <vector>

#include "arena_view.hpp"

namespace dwe::app {

namespace {

// STD_MODEL_SCENE_MATRIX: the letters' scene stands 1000 in front of the eye.
constexpr float LetterSceneDepth = 1000.0F;
// The letters are drawn this much nearer the eye, as large on screen, so the arena and the
// fighters never hide them (the original draws them on their own ordering table).
constexpr float LetterNear = 0.2F;
constexpr float OriginalTurn = 4096.0F;
constexpr float OriginalOne = 4096.0F;

// The original's rotation of an object (RotMatrix with no z): about y, then x.
glm::mat3 originalRotation(std::int32_t x, std::int32_t y) {
  const float ax = static_cast<float>(x) * 2.0F * std::numbers::pi_v<float> / OriginalTurn;
  const float ay = static_cast<float>(y) * 2.0F * std::numbers::pi_v<float> / OriginalTurn;
  const glm::mat3 rx{1.0F,         0.0F, 0.0F,          0.0F,        std::cos(ax),
                     std::sin(ax), 0.0F, -std::sin(ax), std::cos(ax)};
  const glm::mat3 ry{std::cos(ay), 0.0F,         -std::sin(ay), 0.0F,        1.0F,
                     0.0F,         std::sin(ay), 0.0F,          std::cos(ay)};
  return ry * rx;
}

} // namespace

void showLetterModel(FieldScene& scene, std::span<const LetterModel> models, LetterModel shown,
                     const std::optional<game::tournament::LetterScene>& letters,
                     const game::tournament::ShowView& view) {
  for (const LetterModel& model : models) {
    if (model.model == shown.model) {
      continue;
    }
    if (auto id = assets::AssetId::parse(model.model)) {
      scene.setPosedProps(*id, {});
    }
  }
  if (!letters) {
    return;
  }
  const auto id = assets::AssetId::parse(shown.model);
  if (!id) {
    return;
  }
  // The eye's axes in the world: the letters' scene is the eye's own space (x right, y down, z
  // ahead), 1000 ahead (STD_MODEL_SCENE_MATRIX), seen with the scene's projection.
  const glm::vec3 eye = worldOf(view.eye);
  const glm::vec3 ahead = glm::normalize(worldOf(view.target) - eye);
  const glm::vec3 right = glm::normalize(glm::cross(ahead, glm::vec3{0.0F, 1.0F, 0.0F}));
  const glm::vec3 up = glm::cross(right, ahead);
  const glm::mat3 eyeAxes{right, -up, ahead};
  const glm::mat3 toOriginal{1.0F, 0.0F, 0.0F, 0.0F, -1.0F, 0.0F, 0.0F, 0.0F, -1.0F};
  const float depthScale = letters->projection > 0 ? static_cast<float>(view.projection) /
                                                         static_cast<float>(letters->projection)
                                                   : 1.0F;
  // One place per gleam column: a letter whose face shows another column of the gold strip is
  // drawn on its own, its texel coordinates moved, the other letters collapsed.
  std::vector<FieldScene::PropPlace> places;
  const auto placeOf = [&](std::int32_t gleamShift) -> std::vector<glm::mat4>& {
    const glm::vec2 offset{-static_cast<float>(gleamShift), 0.0F};
    for (FieldScene::PropPlace& place : places) {
      if (place.texelOffset == offset) {
        return place.joints;
      }
    }
    places.push_back({.world = glm::mat4{1.0F},
                      .joints = std::vector<glm::mat4>(shown.objects, glm::mat4{0.0F}),
                      .tint = glm::vec4{1.0F},
                      .texelOffset = offset});
    return places.back().joints;
  };
  for (const game::tournament::LetterPose& letter : letters->letters) {
    if (letter.object >= shown.objects) {
      continue;
    }
    const glm::vec3 at{static_cast<float>(letter.location[0]),
                       static_cast<float>(letter.location[1]),
                       (static_cast<float>(letter.location[2]) + LetterSceneDepth) * depthScale};
    const glm::mat3 scale{1.0F, 0.0F, 0.0F, 0.0F, static_cast<float>(letter.scaleY) / OriginalOne,
                          0.0F, 0.0F, 0.0F, 1.0F};
    const glm::mat3 turn = eyeAxes * originalRotation(letter.rotation[0], letter.rotation[1]) *
                           scale * toOriginal * LetterNear;
    glm::mat4 joint{turn};
    joint[3] = glm::vec4{eye + (eyeAxes * at * LetterNear), 1.0F};
    placeOf(letter.gleamShift).at(letter.object) = joint;
  }
  scene.setPosedProps(*id, std::move(places));
}

} // namespace dwe::app
