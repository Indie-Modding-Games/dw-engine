#pragma once

#include <dwe/assets/raise_table.hpp>
#include <dwe/render/model_renderer.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace dwe::app {

// A separate menu instance: loading and posing it never touches the field actor.
class PartnerPortrait {
public:
  static core::Result<std::unique_ptr<PartnerPortrait>> create(render::Renderer& renderer);
  ~PartnerPortrait();
  core::Result<void> load(const assets::VirtualFileSystem& files, std::string_view species,
                          const assets::RaiseRow& raise);
  render::TextureId texture() const { return texture_; }
  render::TextureScene scene();
  void clear();

private:
  PartnerPortrait(render::Renderer& renderer, std::unique_ptr<render::ModelRenderer> models,
                  render::TextureId texture);
  render::Renderer* renderer_;
  std::unique_ptr<render::ModelRenderer> models_;
  render::TextureId texture_;
  std::array<render::Drawer*, 2> drawers_;
  std::optional<render::ModelHandle> model_;
  glm::mat4 world_{1.F};
  std::vector<glm::mat4> joints_;
};

} // namespace dwe::app
