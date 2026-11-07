#pragma once

#include <dwe/assets/model_data.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/render/model_renderer.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace dwe::app {

// The texture the player page's medal view shows the medal in (pause_menu's #medal-figure).
inline constexpr std::string_view MedalFigureTexture = "runtime/medal-figure";

// The medal that turns in the right box of the medals page (game_menu.c: renderDigiviceMedals,
// map_object.c: initializeMedalModel, activateMedalTexture): ETCNA/MEDAL.TMD with the selected
// medal's face, turning about its y axis, seen from the original's MEDAL_VIEW into a texture of
// the whole 320 x 240 screen, which the page lays over itself. It draws only while the page shows
// a held medal.
class MedalFigure {
public:
  static core::Result<std::unique_ptr<MedalFigure>> create(render::Renderer& renderer);
  MedalFigure(const MedalFigure&) = delete;
  MedalFigure& operator=(const MedalFigure&) = delete;
  MedalFigure(MedalFigure&&) = delete;
  MedalFigure& operator=(MedalFigure&&) = delete;
  ~MedalFigure();

  // Loads the medal model (initializeMedalModel); an error when the content lacks it.
  core::Result<void> load(const assets::VirtualFileSystem& files);
  [[nodiscard]] bool loaded() const { return model_.has_value(); }
  [[nodiscard]] render::TextureId texture() const { return texture_; }
  // Its models, for the graphics options (graphics_options.hpp: applyTextureFilter).
  [[nodiscard]] render::ModelRenderer& models() { return *models_; }
  // Puts medal `medal`'s face on the model, when it is not the one on it (activateMedalTexture).
  void show(std::int32_t medal);
  // Turns the medal by the frame's time and gives the frame's scene; only while it is shown.
  render::TextureScene scene(double seconds);

private:
  MedalFigure(render::Renderer& renderer, std::unique_ptr<render::ModelRenderer> models,
              render::TextureId texture);

  render::Renderer* renderer_;
  std::unique_ptr<render::ModelRenderer> models_;
  render::TextureId texture_;
  std::array<render::Drawer*, 2> drawers_;
  std::unique_ptr<assets::ModelData> data_;
  std::optional<render::ModelHandle> model_;
  // The face on the model; none until one is shown.
  std::optional<std::int32_t> medal_;
  // MEDAL_ROTATION.vy and the time toward its next turn.
  std::int32_t turn_ = 0;
  double turnSeconds_ = 0.0;
};

} // namespace dwe::app
