#include "partner_portrait.hpp"

#include <dwe/assets/content_id.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/render/camera.hpp>

#include <cmath>
#include <format>

namespace dwe::app {

core::Result<std::unique_ptr<PartnerPortrait>> PartnerPortrait::create(render::Renderer& renderer) {
  auto models = render::ModelRenderer::create(renderer);
  if (!models)
    return std::unexpected{models.error()};
  auto texture = renderer.createSceneTexture({.width = 320, .height = 240});
  if (!texture)
    return std::unexpected{texture.error()};
  return std::unique_ptr<PartnerPortrait>{
      new PartnerPortrait{renderer, std::move(*models), *texture}};
}

PartnerPortrait::PartnerPortrait(render::Renderer& renderer,
                                 std::unique_ptr<render::ModelRenderer> models,
                                 render::TextureId texture)
    : renderer_(&renderer), models_(std::move(models)), texture_(texture),
      drawers_{&models_->opaque(), &models_->translucent()} {
  // The extractor rotates original coordinates (x,y,z) to glTF (x,-y,-z).
  models_->setCamera(
      {.view = glm::lookAt(glm::vec3{1300, 0, 3280}, glm::vec3{0}, glm::vec3{0, 1, 0}),
       .projection =
           render::perspective(2.F * std::atan(120.F / 512.F), 320.F / 240.F, 10.F, 20000.F),
       .viewport = {}});
  render::ModelLighting light{};
  // Field/new-game ambient persists when the menu replaces the three flat lights.
  light.ambient = glm::vec3{0.5F};
  light.lights[0] = {.direction = glm::normalize(glm::vec3{100, -100, -100}),
                     .color = glm::vec3{0.5F}};
  models_->setLighting(light);
}

PartnerPortrait::~PartnerPortrait() {
  renderer_->destroyTexture(texture_);
}

core::Result<void> PartnerPortrait::load(const assets::VirtualFileSystem& files,
                                         std::string_view species, const assets::RaiseRow& raise,
                                         float scale) {
  clear();
  const auto id =
      assets::AssetId::parse(std::format("digimon/{}/model", assets::contentAssetPath(species)));
  if (!id)
    return std::unexpected{id.error()};
  auto data = assets::loadModel(files, *id);
  if (!data)
    return std::unexpected{data.error()};
  data_ = std::make_unique<assets::ModelData>(std::move(*data));
  pose_.emplace(*data_);
  clip_ = 0;
  if (!pose_->play(clip_)) {
    clear();
    return core::fail("Status model has no idle clip 0");
  }
  // The extracted clip's time zero already represents the original's first tick.
  auto model = models_->add(*data_);
  if (!model) {
    clear();
    return std::unexpected{model.error()};
  }
  model_ = *model;
  static_cast<void>(pose_->advance(0.0F));
  const auto joints = pose_->jointMatrices();
  joints_.assign(joints.begin(), joints.end());
  world_ = glm::scale(glm::translate(glm::mat4{1.F}, glm::vec3{static_cast<float>(raise.view[0]),
                                                               -static_cast<float>(raise.view[1]),
                                                               -static_cast<float>(raise.view[2])}),
                      glm::vec3{scale});
  return {};
}

void PartnerPortrait::play(std::uint32_t clip) {
  if (!pose_ || clip == clip_)
    return;
  clip_ = clip;
  if (!pose_->play(clip))
    static_cast<void>(pose_->play(0));
}

void PartnerPortrait::clear() {
  models_->clear();
  if (model_)
    models_->remove(*model_);
  model_.reset();
  pose_.reset();
  data_.reset();
  clip_ = 0;
  joints_.clear();
}

render::TextureScene PartnerPortrait::scene(double seconds) {
  models_->clear();
  if (pose_) {
    // The clip's root motion is left out: the model stays at its place on the page.
    static_cast<void>(pose_->advance(static_cast<float>(seconds)));
    if (pose_->finished()) {
      static_cast<void>(pose_->play(*pose_->clip()));
      static_cast<void>(pose_->advance(0.0F));
    }
    const auto joints = pose_->jointMatrices();
    joints_.assign(joints.begin(), joints.end());
  }
  if (model_)
    models_->submit({.model = *model_, .world = world_, .joints = joints_});
  return {.target = texture_, .drawers = drawers_, .keep = false};
}

} // namespace dwe::app
