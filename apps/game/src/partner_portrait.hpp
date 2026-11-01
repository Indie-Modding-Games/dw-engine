#pragma once

#include <dwe/animation/animation_player.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/assets/raise_table.hpp>
#include <dwe/render/model_renderer.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace dwe::app {

// A separate menu instance: loading and posing it never touches the field actor. The original poses
// the partner in the first frame of its idle (game_menu.c: renderDigiviceEntity); here it plays a
// clip the game picks (the status page: its idle, or the gesture of its need).
class PartnerPortrait {
public:
  static core::Result<std::unique_ptr<PartnerPortrait>> create(render::Renderer& renderer);
  ~PartnerPortrait();
  core::Result<void> load(const assets::VirtualFileSystem& files, std::string_view species,
                          const assets::RaiseRow& raise);
  render::TextureId texture() const { return texture_; }
  // Plays this clip (an original animation id) from now on, from its start when it changes; a clip
  // the model lacks plays its idle. A clip that does not repeat starts again once it ends.
  void play(std::uint32_t clip);
  // Moves the clip on by these seconds and draws the model.
  render::TextureScene scene(double seconds = 0.0);
  void clear();

private:
  PartnerPortrait(render::Renderer& renderer, std::unique_ptr<render::ModelRenderer> models,
                  render::TextureId texture);
  render::Renderer* renderer_;
  std::unique_ptr<render::ModelRenderer> models_;
  render::TextureId texture_;
  std::array<render::Drawer*, 2> drawers_;
  std::unique_ptr<assets::ModelData> data_;
  std::optional<animation::AnimationPlayer> pose_;
  std::uint32_t clip_ = 0;
  std::optional<render::ModelHandle> model_;
  glm::mat4 world_{1.F};
  std::vector<glm::mat4> joints_;
};

} // namespace dwe::app
