#pragma once

#include <dwe/core/error.hpp>
#include <dwe/render/drawer.hpp>
#include <dwe/render/renderer.hpp>

#include <cstddef>
#include <memory>

namespace dwe::ui {

// Draws the UI with the renderer, in the UI pass. Ui::update() records the frame's geometry and
// creates the textures it needs (outside the frame); draw() replays it. The UiRenderer must outlive
// the Ui that uses it, and the renderer must outlive both.
class UiRenderer final : public render::Drawer {
public:
  static core::Result<std::unique_ptr<UiRenderer>> create(render::Renderer& renderer);

  UiRenderer(const UiRenderer&) = delete;
  UiRenderer& operator=(const UiRenderer&) = delete;
  UiRenderer(UiRenderer&&) = delete;
  UiRenderer& operator=(UiRenderer&&) = delete;
  ~UiRenderer() override;

  void draw(render::DrawContext& context) override;

  // How many draws the last Ui::update() recorded.
  [[nodiscard]] std::size_t drawCount() const;

  // The state shared with the module's RmlUi render interface.
  struct Impl;
  [[nodiscard]] Impl& impl() const { return *impl_; }

private:
  explicit UiRenderer(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

} // namespace dwe::ui
