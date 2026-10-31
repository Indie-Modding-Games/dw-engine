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
// The fade to white is the same rectangle added (FADE_MODE 1, B + F): whiteness 1 turns it white.
class ScreenFade final : public Drawer {
public:
  static core::Result<ScreenFade> create(Renderer& renderer);

  // How much to subtract, from 0 to 1 (clamped).
  void setDarkness(float darkness);
  [[nodiscard]] float darkness() const { return darkness_; }
  // How much to add, from 0 to 1 (clamped).
  void setWhiteness(float whiteness);
  [[nodiscard]] float whiteness() const { return whiteness_; }

  void draw(DrawContext& context) override;

private:
  ScreenFade(QuadBatch quads, QuadBatch whiteQuads, TextureId white)
      : quads_(std::move(quads)), whiteQuads_(std::move(whiteQuads)), white_(white) {}

  static void cover(QuadBatch& quads, DrawContext& context, float amount, TextureId white);

  QuadBatch quads_;
  QuadBatch whiteQuads_;
  TextureId white_;
  float darkness_ = 0.0F;
  float whiteness_ = 0.0F;
};

// The four vertices' color a ScreenFade of `darkness` (or `whiteness`) draws with: the grey to
// subtract (or add), and alpha 1.
[[nodiscard]] glm::vec4 screenFadeColor(float darkness);

} // namespace dwe::render
