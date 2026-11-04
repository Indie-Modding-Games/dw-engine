#include "model_viewer.hpp"

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/math.hpp>
#include <dwe/render/camera.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <numbers>
#include <tuple>
#include <utility>

#include "chibi_style.hpp"

namespace dwe::app {

namespace {

// Room between actors, in world units (100 = one collision cell): at least this, or the widest
// model and a margin.
constexpr float Spacing = 350.0F;
constexpr float Margin = 60.0F;
constexpr float CameraTurnSeconds = 40.0F;
constexpr float FieldOfView = 0.6F;
constexpr float NearPlane = 10.0F;
constexpr float FarPlane = 20000.0F;
// The camera looks a little down from this share of its distance above the models' middle.
constexpr float CameraRise = 0.15F;
// The tallest model fills this share of the view's height, and the row this share of its width.
constexpr float FillHeight = 0.8F;
constexpr float FillWidth = 0.95F;

} // namespace

core::Result<std::unique_ptr<ModelViewer>>
ModelViewer::create(render::Renderer& renderer, const std::filesystem::path& content,
                    const std::filesystem::path& gamePack, std::span<const std::string> characters,
                    const ModelViewerStyle& style) {
  auto models = render::ModelRenderer::create(renderer);
  if (!models) {
    return std::unexpected{models.error()};
  }
  std::unique_ptr<ModelViewer> viewer{new ModelViewer{std::move(*models)}};
  viewer->style_ = style;
  // The models are read once, so the file systems and the VFS live only while loading.
  const assets::DiskFileSystem disk;
  auto packs = assets::findPacks(disk, content);
  if (!packs) {
    return std::unexpected{packs.error()};
  }
  packs->packs.push_back({.path = gamePack, .kind = assets::FoundPack::Kind::Folder});
  // The content folder and the team's game pack are the engine's own.
  const auto opened = assets::openPacks(disk, packs->packs, assets::PackOrigin::Engine);
  if (!opened) {
    return std::unexpected{opened.error()};
  }
  auto vfs = assets::VirtualFileSystem::mount(opened->locations);
  if (!vfs) {
    return std::unexpected{vfs.error()};
  }
  const ChibiStyleData chibiStyle = loadChibiStyleData(*vfs);
  for (std::size_t row = 0; row < characters.size(); ++row) {
    const auto id = assets::AssetId::parse(characters[row]);
    if (!id) {
      return std::unexpected{id.error()};
    }
    // A species model plays the partner's idle and walk, as the field draws a partner or an NPC.
    const bool species = !chibiSpeciesOf(*id).empty();
    auto character =
        assets::loadCharacterModel(*vfs, species ? *assets::AssetId::parse("models/partner") : *id);
    if (!character) {
      return std::unexpected{character.error()};
    }
    if (species) {
      character->model = *id;
      std::erase_if(character->animations, [](const auto& animation) {
        return animation.first != "idle" && animation.first != "walk";
      });
    }
    auto model = assets::loadModel(*vfs, character->model);
    if (!model) {
      return std::unexpected{model.error()};
    }
    if (species) {
      std::ignore = assets::dropMissingClips(*character, *model);
    } else if (auto checked = assets::checkCharacterModel(*character, *model); !checked) {
      return std::unexpected{checked.error()};
    }
    const auto addStart = std::chrono::steady_clock::now();
    auto handle = viewer->models_->add(*model);
    if (!handle) {
      return std::unexpected{handle.error()};
    }
    const std::chrono::duration<double, std::milli> addTime =
        std::chrono::steady_clock::now() - addStart;
    viewer->data_.push_back(std::make_unique<assets::ModelData>(std::move(*model)));
    viewer->handles_.push_back(*handle);
    const assets::ModelData& data = *viewer->data_.back();
    viewer->rigs_.push_back(chibiRigOf(character->model, data, chibiStyle));
    // The model's size in its rest pose, for the room between actors and the camera.
    glm::vec3 low{0.0F};
    glm::vec3 high{0.0F};
    for (const assets::ModelPrimitive& primitive : data.primitives) {
      for (const assets::ModelVertex& vertex : primitive.vertices) {
        low = glm::min(low, vertex.position);
        high = glm::max(high, vertex.position);
      }
    }
    const float spacing = std::max(Spacing, high.x - low.x + Margin);
    const std::size_t looks = style.chibi ? 2 : 1;
    const std::size_t columns = character->animations.size() * looks;
    viewer->rowWidth_ = std::max(viewer->rowWidth_, static_cast<float>(columns) * spacing);
    viewer->tallest_ = std::max(viewer->tallest_, high.y);
    float column = -0.5F * static_cast<float>(columns - 1);
    for (const auto& [animation, clip] : character->animations) {
      for (std::size_t look = 0; look < looks; ++look) {
        Actor actor{
            .model = viewer->data_.size() - 1,
            .position = {column * spacing, 0.0F, (static_cast<float>(row) - 0.5F) * Spacing},
            .animation = animation,
            .player = std::make_unique<animation::AnimationPlayer>(data),
            .chibi = look == 1};
        if (!actor.player->play(clip)) {
          return core::fail(std::format("{} has no clip {}", character->model.str(), clip));
        }
        viewer->actors_.push_back(std::move(actor));
        column += 1.0F;
      }
    }
    core::logInfo("model viewer: {} ({}) with {} animations, uploaded in {:.2f} ms",
                  characters[row], character->model.str(), character->animations.size(),
                  addTime.count());
  }
  return viewer;
}

void ModelViewer::update(double seconds, rhi::Extent extent) {
  const auto step = static_cast<float>(seconds - time_);
  time_ = seconds;
  // A frame that was not drawn (a minimised window) left its instances behind.
  models_->clear();
  for (Actor& actor : actors_) {
    // In place: the root motion is left out, as the field decides how an entity moves.
    std::ignore = actor.player->advance(step);
    std::span<const glm::mat4> joints = actor.player->jointMatrices();
    if (actor.chibi) {
      const assets::ModelData& model = *data_[actor.model];
      chibiWorld_.resize(model.nodes.size());
      chibiJoints_.resize(joints.size());
      animation::computeChibiJointMatrices(model, rigs_[actor.model], actor.player->pose(), joints,
                                           chibiWorld_, chibiJoints_);
      joints = chibiJoints_;
    }
    models_->submit(
        {.model = handles_[actor.model],
         .world = glm::rotate(glm::translate(glm::mat4{1.0F}, actor.position),
                              glm::radians(style_.yaw.value_or(0.0F)), glm::vec3{0.0F, 1.0F, 0.0F}),
         .joints = joints});
  }
  // A fixed turn turns the models instead, so the row stays across the view.
  const float angle = style_.yaw ? 0.0F
                                 : static_cast<float>(seconds) * 2.0F * std::numbers::pi_v<float> /
                                       CameraTurnSeconds;
  const float aspect = extent.height > 0
                           ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                           : 1.0F;
  // Far enough for the tallest model and the whole row (seen from the front) to fit the target.
  const float viewHeight = 2.0F * std::tan(FieldOfView / 2.0F);
  const float distance = style_.frame ? *style_.frame / viewHeight
                                      : std::max(tallest_ / (FillHeight * viewHeight),
                                                 rowWidth_ / (FillWidth * viewHeight * aspect));
  const float middle = style_.frame ? *style_.frame * FillHeight / 2.0F : tallest_ / 2.0F;
  const glm::vec3 target{0.0F, middle, 0.0F};
  const glm::vec3 eye = target + glm::vec3{distance * std::sin(angle), CameraRise * distance,
                                           distance * std::cos(angle)};
  models_->setCamera(
      {.view = glm::lookAt(eye, target, glm::vec3{0, 1, 0}),
       .projection = render::perspective(FieldOfView, aspect, NearPlane, FarPlane),
       // An empty viewport is the whole target, which may have been resized since `extent`.
       .viewport = {}});
}

} // namespace dwe::app
