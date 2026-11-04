#include "save_figure.hpp"

#include <dwe/assets/content_id.hpp>
#include <dwe/game/field/partner_follow.hpp>
#include <dwe/render/camera.hpp>

#include <cmath>
#include <format>

namespace dwe::app {
namespace {

// The figure's strip, three times as wide as tall: the document draws it 72dp by 24dp, so this is
// sharp up to a window four times 720 lines tall.
constexpr std::uint32_t TextureWidth = 288;
constexpr std::uint32_t TextureHeight = 96;
constexpr float Aspect = static_cast<float>(TextureWidth) / static_cast<float>(TextureHeight);
// The species' height (Gabumon's 260 in the species table): the strip shows it whole, head to
// feet, with a little room above for the run's bounce.
constexpr float FigureHeight = 260.F;
constexpr float HalfView = 0.5F * FigureHeight;
// Far away with a narrow view: nearly flat, so it looks the same at either end of its lane.
constexpr float ViewDistance = 20.F * FigureHeight;
// Its lane: the strip's width less about half its body at each end.
constexpr float LaneHalf = (Aspect * HalfView) - (0.7F * FigureHeight);

} // namespace

core::Result<std::unique_ptr<SaveFigure>> SaveFigure::create(render::Renderer& renderer) {
  auto models = render::ModelRenderer::create(renderer);
  if (!models) {
    return std::unexpected{models.error()};
  }
  auto texture = renderer.createSceneTexture({.width = TextureWidth, .height = TextureHeight});
  if (!texture) {
    return std::unexpected{texture.error()};
  }
  return std::unique_ptr<SaveFigure>{new SaveFigure{renderer, std::move(*models), *texture}};
}

SaveFigure::SaveFigure(render::Renderer& renderer, std::unique_ptr<render::ModelRenderer> models,
                       render::TextureId texture)
    : renderer_(&renderer), models_(std::move(models)), texture_(texture),
      drawers_{&models_->opaque(), &models_->translucent()}, runner_(LaneHalf) {
  // From the side of its lane, level with its middle: the figure runs across the view in profile.
  // The extractor rotates original coordinates (x,y,z) to glTF (x,-y,-z): up is +y, the figure
  // stands on y 0 and faces +z at yaw 0.
  const glm::vec3 middle{0.F, 0.5F * FigureHeight, 0.F};
  models_->setCamera(
      {.view = glm::lookAt(middle + glm::vec3{0.F, 0.F, ViewDistance}, middle, glm::vec3{0, 1, 0}),
       .projection = render::perspective(2.F * std::atan(HalfView / ViewDistance), Aspect,
                                         ViewDistance - (2.F * FigureHeight),
                                         ViewDistance + (2.F * FigureHeight)),
       .viewport = {}});
  render::ModelLighting light{};
  light.ambient = glm::vec3{0.5F};
  light.lights[0] = {.direction = glm::normalize(glm::vec3{100, -100, -100}),
                     .color = glm::vec3{0.5F}};
  models_->setLighting(light);
}

SaveFigure::~SaveFigure() {
  models_->clear();
  if (model_) {
    models_->remove(*model_);
  }
  renderer_->destroyTexture(texture_);
}

core::Result<void> SaveFigure::load(const assets::VirtualFileSystem& files) {
  const auto id =
      assets::AssetId::parse(std::format("digimon/{}/model", assets::contentAssetPath(Species)));
  if (!id) {
    return std::unexpected{id.error()};
  }
  auto data = assets::loadModel(files, *id);
  if (!data) {
    return std::unexpected{data.error()};
  }
  data_ = std::make_unique<assets::ModelData>(std::move(*data));
  pose_.emplace(*data_);
  // The partner's walk and run (partner_follow.hpp), one for each gait of the lane, the other in
  // place of a missing one, else its idle; the lane moves it, so the clips' root motion is left
  // out.
  const auto walk = static_cast<std::uint32_t>(game::field::PartnerWalkClip);
  const auto run = static_cast<std::uint32_t>(game::field::PartnerRunClip);
  const bool hasWalk = pose_->play(walk);
  const bool hasRun = pose_->play(run);
  walkClip_ = hasWalk ? walk : run;
  runClip_ = hasRun ? run : walk;
  if (!hasWalk && !hasRun) {
    walkClip_ = runClip_ = 0;
  }
  clip_ = clipOf(runner_.gait());
  if (!pose_->play(clip_)) {
    pose_.reset();
    data_.reset();
    return core::fail(std::format("{}: no walk, run or idle clip", Species));
  }
  auto model = models_->add(*data_);
  if (!model) {
    pose_.reset();
    data_.reset();
    return std::unexpected{model.error()};
  }
  model_ = *model;
  static_cast<void>(pose_->advance(0.0F));
  return {};
}

render::TextureScene SaveFigure::scene(double seconds) {
  models_->clear();
  if (model_ && pose_) {
    // The legs stop while it turns at an end, unless the next lap's gait is another clip: then
    // the turn blends into it, from the stride where the last lap ended to the new clip's start.
    if (!runner_.turning() || switching_) {
      static_cast<void>(pose_->advance(static_cast<float>(seconds)));
    }
    const std::uint32_t lap = runner_.lap();
    runner_.advance(static_cast<float>(seconds));
    if (runner_.lap() != lap) {
      const std::uint32_t clip = clipOf(runner_.gait());
      switching_ = clip != clip_ && pose_->play(clip, LaneRunner::TurnSeconds);
      if (switching_) {
        clip_ = clip;
      }
    }
    if (!runner_.turning()) {
      switching_ = false;
    }
    const auto joints = pose_->jointMatrices();
    joints_.assign(joints.begin(), joints.end());
    const glm::mat4 world =
        glm::rotate(glm::translate(glm::mat4{1.F}, glm::vec3{runner_.x(), 0.F, 0.F}), runner_.yaw(),
                    glm::vec3{0.F, 1.F, 0.F});
    models_->submit({.model = *model_, .world = world, .joints = joints_});
  }
  return {.target = texture_, .drawers = drawers_, .keep = false};
}

} // namespace dwe::app
