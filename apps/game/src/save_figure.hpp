#pragma once

#include <dwe/animation/animation_player.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/render/model_renderer.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "save_indicator.hpp"

namespace dwe::app {

// The save indicator's figure (save_indicator.hpp): a species' model from the player's content,
// whole and small, walking or running in profile back and forth along a short lane (LaneRunner),
// drawn into a strip texture of its own (SaveFigureTexture) the indicator's document shows. It
// draws only while the indicator is on screen.
class SaveFigure {
public:
  // The species the indicator shows.
  static constexpr std::string_view Species = "gabu";

  static core::Result<std::unique_ptr<SaveFigure>> create(render::Renderer& renderer);
  SaveFigure(const SaveFigure&) = delete;
  SaveFigure& operator=(const SaveFigure&) = delete;
  SaveFigure(SaveFigure&&) = delete;
  SaveFigure& operator=(SaveFigure&&) = delete;
  ~SaveFigure();

  // Loads the species' model and its walk and run; an error when the content lacks them all.
  core::Result<void> load(const assets::VirtualFileSystem& files);
  [[nodiscard]] bool loaded() const { return model_.has_value(); }
  [[nodiscard]] render::TextureId texture() const { return texture_; }
  // Moves the run on and gives the frame's scene; only while the indicator is shown.
  render::TextureScene scene(double seconds);

private:
  SaveFigure(render::Renderer& renderer, std::unique_ptr<render::ModelRenderer> models,
             render::TextureId texture);

  render::Renderer* renderer_;
  std::unique_ptr<render::ModelRenderer> models_;
  render::TextureId texture_;
  std::array<render::Drawer*, 2> drawers_;
  // The model's data outlives the player that poses it.
  std::unique_ptr<assets::ModelData> data_;
  std::optional<animation::AnimationPlayer> pose_;
  std::optional<render::ModelHandle> model_;
  std::vector<glm::mat4> joints_;
  LaneRunner runner_;
  // The clips of the lane's two gaits, the one playing, and whether a turn is blending into it.
  std::uint32_t walkClip_ = 0;
  std::uint32_t runClip_ = 0;
  std::uint32_t clip_ = 0;
  bool switching_ = false;

  [[nodiscard]] std::uint32_t clipOf(LaneRunner::Gait gait) const {
    return gait == LaneRunner::Gait::Run ? runClip_ : walkClip_;
  }
};

} // namespace dwe::app
