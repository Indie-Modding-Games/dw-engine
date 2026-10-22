#pragma once

#include <dwe/core/error.hpp>
#include <dwe/render/drawer.hpp>
#include <dwe/render/quad_batch.hpp>
#include <dwe/render/renderer.hpp>

#include <utility>

namespace dwe::render {

// A grey subtracted from the whole target, over everything drawn before it in the pass: the
// original's fade (fade.c: renderFade, a full-screen rectangle in the subtractive mode, B - F), not
// a blend toward black. Darkness 0 draws nothing; 1 subtracts white, so the screen goes black.
class ScreenFade final : public Drawer {
public:
  static core::Result<ScreenFade> create(Renderer& renderer);

  // How much to subtract, from 0 to 1 (clamped).
  void setDarkness(float darkness);
  [[nodiscard]] float darkness() const { return darkness_; }

  void draw(DrawContext& context) override;

private:
  ScreenFade(QuadBatch quads, TextureId white) : quads_(std::move(quads)), white_(white) {}

  QuadBatch quads_;
  TextureId white_;
  float darkness_ = 0.0F;
};

// The four vertices' color a ScreenFade of `darkness` draws with: the grey to subtract, and
// alpha 1.
[[nodiscard]] glm::vec4 screenFadeColor(float darkness);

} // namespace dwe::render
