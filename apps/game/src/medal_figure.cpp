#include "medal_figure.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/math.hpp>
#include <dwe/render/camera.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstddef>
#include <format>
#include <numbers>
#include <vector>

#include "medal_sheet.hpp"

namespace dwe::app {
namespace {

constexpr std::string_view MedalModel = "model/etcna/medal";
// The original's screen.
constexpr std::uint32_t ScreenWidth = 320;
constexpr std::uint32_t ScreenHeight = 240;
// game_menu.c: MEDAL_VIEW looks from (-1050, 220, -10000) at (-1050, 220, 0), with the projection
// distance 0x400 (GsSetProjection), in the original's axes (y down, turned to the world's).
constexpr glm::vec3 ViewFrom{-1050.F, -220.F, 10000.F};
constexpr glm::vec3 ViewAt{-1050.F, -220.F, 0.F};
constexpr float ProjectionDistance = 1024.F;
// The original's menu draws 20 frames a second, as its field (main.c: gameLoop waits VSync(3)).
constexpr double OriginalFrameSeconds = 1.0 / 20.0;
constexpr std::size_t BytesPerTexel = 4;

// The RGBA8 pixels of a region of an image; empty when it does not fit.
std::vector<std::byte> regionOf(const assets::Image& image, const rhi::Rect& region) {
  if (region.x < 0 || region.y < 0 ||
      static_cast<std::uint32_t>(region.x) + region.width > image.width ||
      static_cast<std::uint32_t>(region.y) + region.height > image.height) {
    return {};
  }
  std::vector<std::byte> pixels;
  pixels.reserve(std::size_t{region.width} * region.height * BytesPerTexel);
  for (std::uint32_t row = 0; row < region.height; ++row) {
    const std::size_t first = ((static_cast<std::size_t>(region.y) + row) * image.width +
                               static_cast<std::size_t>(region.x)) *
                              BytesPerTexel;
    pixels.insert(pixels.end(), image.pixels.begin() + static_cast<std::ptrdiff_t>(first),
                  image.pixels.begin() + static_cast<std::ptrdiff_t>(
                                             first + std::size_t{region.width} * BytesPerTexel));
  }
  return pixels;
}

} // namespace

core::Result<std::unique_ptr<MedalFigure>> MedalFigure::create(render::Renderer& renderer) {
  auto models = render::ModelRenderer::create(renderer);
  if (!models) {
    return std::unexpected{models.error()};
  }
  auto texture = renderer.createSceneTexture({.width = ScreenWidth, .height = ScreenHeight});
  if (!texture) {
    return std::unexpected{texture.error()};
  }
  return std::unique_ptr<MedalFigure>{new MedalFigure{renderer, std::move(*models), *texture}};
}

MedalFigure::MedalFigure(render::Renderer& renderer, std::unique_ptr<render::ModelRenderer> models,
                         render::TextureId texture)
    : renderer_(&renderer), models_(std::move(models)), texture_(texture),
      drawers_{&models_->opaque(), &models_->translucent()} {
  const float halfHeight = 0.5F * static_cast<float>(ScreenHeight);
  models_->setCamera(
      {.view = glm::lookAt(ViewFrom, ViewAt, glm::vec3{0, 1, 0}),
       .projection = render::perspective(
           2.F * std::atan(halfHeight / ProjectionDistance),
           static_cast<float>(ScreenWidth) / static_cast<float>(ScreenHeight), 10.F, 20000.F),
       .viewport = {}});
  // The menu's figures' light (partner_portrait.cpp).
  render::ModelLighting light{};
  light.ambient = glm::vec3{0.5F};
  light.lights[0] = {.direction = glm::normalize(glm::vec3{100, -100, -100}),
                     .color = glm::vec3{0.5F}};
  models_->setLighting(light);
}

MedalFigure::~MedalFigure() {
  models_->clear();
  if (model_) {
    models_->remove(*model_);
  }
  renderer_->destroyTexture(texture_);
}

core::Result<void> MedalFigure::load(const assets::VirtualFileSystem& files) {
  const auto id = assets::AssetId::parse(MedalModel);
  if (!id) {
    return std::unexpected{id.error()};
  }
  auto data = assets::loadModel(files, *id);
  if (!data) {
    return std::unexpected{data.error()};
  }
  if (data->images.empty()) {
    return core::fail(std::format("{}: no image", MedalModel));
  }
  data_ = std::make_unique<assets::ModelData>(std::move(*data));
  auto model = models_->add(*data_);
  if (!model) {
    data_.reset();
    return std::unexpected{model.error()};
  }
  model_ = *model;
  medal_.reset();
  return {};
}

void MedalFigure::show(std::int32_t medal) {
  if (!model_ || !data_ || medal_ == medal) {
    return;
  }
  const std::vector<std::byte> face = regionOf(data_->images.front(), medalFace(medal));
  if (face.empty()) {
    core::logWarning("medal figure: no face for medal {}", medal);
    return;
  }
  if (auto updated = models_->updateImage(*model_, 0, MedalSlot, face); !updated) {
    core::logWarning("medal figure: {}", updated.error().message);
    return;
  }
  core::logDebug("[port:menus] activateMedalTexture (map_object.c:1193): medal {}", medal);
  medal_ = medal;
}

render::TextureScene MedalFigure::scene(double seconds) {
  models_->clear();
  if (model_) {
    turnSeconds_ += seconds;
    while (turnSeconds_ >= OriginalFrameSeconds) {
      turnSeconds_ -= OriginalFrameSeconds;
      turn_ = (turn_ + MedalTurnPerFrame) % core::OriginalAnglePerTurn;
    }
    // The original's y turn is the world's the other way (core::fromOriginalAxes flips y and z).
    const float yaw = -static_cast<float>(turn_) * 2.F * std::numbers::pi_v<float> /
                      static_cast<float>(core::OriginalAnglePerTurn);
    models_->submit({.model = *model_,
                     .world = glm::rotate(glm::mat4{1.F}, yaw, glm::vec3{0.F, 1.F, 0.F}),
                     .joints = {}});
  }
  return {.target = texture_, .drawers = drawers_, .keep = false};
}

} // namespace dwe::app
