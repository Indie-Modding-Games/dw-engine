#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/image/tim.hpp>
#include <dwe/formats/model/gltf.hpp>
#include <dwe/formats/model/tmd.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

// The textures of a model, found the way the console finds them: each textured primitive addresses
// VRAM through its texture page, UVs and CLUT position, so the extractor rebuilds what the original
// has uploaded there when it draws the model and reads the primitive's texels and palette from
// that.
namespace dwe::dwx {

// The images and palettes the original leaves in VRAM for a model. What is added later covers what
// was added before, as a later upload does.
class VramScene {
public:
  // A TIM uploaded to its own rectangles: its pixels and, if it has one, its CLUT.
  void addTim(const formats::image::Tim& tim);
  // Only the pixels of a TIM.
  void addPixels(const formats::image::Tim& tim);
  // A run of colours at a VRAM position: one CLUT row or part of one.
  void addPalette(std::uint16_t x, std::uint16_t y, std::vector<formats::image::Color15> colors);

  struct Palette {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::vector<formats::image::Color15> colors;
  };

  [[nodiscard]] const std::vector<formats::image::Tim>& images() const { return images_; }
  [[nodiscard]] const std::vector<Palette>& palettes() const { return palettes_; }

private:
  std::vector<formats::image::Tim> images_;
  std::vector<Palette> palettes_;
};

// The textures to give the glTF writer. The PNGs live here; the textures point at them.
struct ModelTextures {
  std::vector<formats::model::ModelTexture> textures;
  std::deque<std::vector<std::byte>> pngs;
  std::size_t texturedPrimitives = 0;
  std::size_t resolvedPrimitives = 0;
  // The colour of every texel inside the UV box of each resolved primitive, primitive after
  // primitive, as the PNG keeps it: two scenes that give the same texels draw the model the same.
  std::vector<formats::image::Rgba8> texels;
  // The primitives whose texels or palette are not in the scene, grouped by page and palette. They
  // keep their UVs on their texture page, without an image.
  std::vector<std::string> unresolved;
};

// Finds every textured primitive's image and palette in the scene and draws each image with each
// palette in use as an indexed PNG (colour 0x0000 transparent). Fails only when a PNG cannot be
// written.
core::Result<ModelTextures> textureModel(const formats::model::Tmd& tmd, const VramScene& scene);

} // namespace dwe::dwx
