#pragma once

#include <dwe/core/error.hpp>
#include <dwe/render/drawer.hpp>
#include <dwe/render/quad_batch.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/rhi/pipeline.hpp>

#include <vector>

namespace dwe::app {

// The sharpen pass after the movie's upscale (movie_sharpen.slang): the scene, drawn into a texture
// the window's size, sharpened into the window.
class MovieSharpen final : public render::Drawer {
public:
  explicit MovieSharpen(rhi::GraphicsPipeline pipeline) : pipeline_(std::move(pipeline)) {}

  void draw(render::DrawContext& context) override;

private:
  rhi::GraphicsPipeline pipeline_;
};

// Draws a movie's picture (an engine improvement on its scaling; docs/architecture/peliculas.md,
// «Escalado»). With the texture filter «upscaled» the picture goes through an edge-adaptive
// upscale to the window's pixels (movie_upscale.slang) and a sharpen pass after it (post());
// otherwise it is stretched with linear filtering, the look movies always had. It takes quads as a
// QuadBatch does.
class MovieDrawer final : public render::Drawer {
public:
  static core::Result<MovieDrawer> create(render::Renderer& renderer);

  void add(const render::Quad& quad);
  void clear();

  // Whether the picture is upscaled and sharpened (the texture filter «upscaled»).
  void setUpscaled(bool upscaled) { upscaled_ = upscaled; }
  [[nodiscard]] bool upscaled() const { return upscaled_; }

  // The frame's post pass: the sharpen when the picture is upscaled, nothing otherwise.
  [[nodiscard]] render::Drawer* post();

  void draw(render::DrawContext& context) override;

private:
  MovieDrawer(render::QuadBatch stretched, rhi::GraphicsPipeline upscale, MovieSharpen sharpen)
      : stretched_(std::move(stretched)), upscale_(std::move(upscale)),
        sharpen_(std::move(sharpen)) {}

  render::QuadBatch stretched_;
  rhi::GraphicsPipeline upscale_;
  MovieSharpen sharpen_;
  std::vector<render::QuadVertex> vertices_;
  bool upscaled_ = false;
};

} // namespace dwe::app
