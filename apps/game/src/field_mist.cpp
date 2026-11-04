#include "field_mist.hpp"

#include <dwe/assets/indexed_image.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace dwe::app {

namespace {

// Where the back layer's left edges, the front layer's right edges and the rows' top edges stay:
// each moves one span (two screens) back when it reaches its limit (renderMist).
constexpr std::int32_t SpanX = 2 * MistLayers::ScreenWidth;
constexpr std::int32_t SpanY = 2 * MistLayers::ScreenHeight;
constexpr std::int32_t BackFirst = -0x1DF;
constexpr std::int32_t BackLimit = 0xA0;
constexpr std::int32_t FrontFirst = -0x9F;
constexpr std::int32_t FrontLimit = 0x1E0;
constexpr std::int32_t RowsFirst = -0x167;
constexpr std::int32_t RowsLimit = 0x78;
// The front layer moves cameraDelta * 12 / 10.
constexpr std::int32_t FrontSpeedNumerator = 12;
constexpr std::int32_t FrontSpeedDenominator = 10;

// One step back into [first, limit), as renderMist does: a single span each way, so a jump of more
// than a span (a new map's camera) takes a few frames to come back.
std::int16_t wrapOnce(std::int32_t value, std::int32_t first, std::int32_t limit,
                      std::int32_t span) {
  auto wrapped = static_cast<std::int16_t>(value);
  if (wrapped >= limit) {
    wrapped = static_cast<std::int16_t>(wrapped - span);
  }
  if (wrapped < first) {
    wrapped = static_cast<std::int16_t>(wrapped + span);
  }
  return wrapped;
}

// The VRAM column of the mist's texture page (GetTPage(0, mode, 0x2c0, 0)).
constexpr std::int32_t MistPageX = 0x2C0;

} // namespace

void MistLayers::frame(glm::ivec2 camera, bool evenFrame) {
  const auto deltaX = static_cast<std::int16_t>(previousCamera_.x - camera.x);
  const auto deltaY = static_cast<std::int16_t>(previousCamera_.y - camera.y);
  previousCamera_ = camera;

  const std::int32_t drift = evenFrame ? 1 : 0;
  backX_[0] = wrapOnce(backX_[0] + deltaX - drift, BackFirst, BackLimit, SpanX);
  backX_[1] = wrapOnce(backX_[0] + ScreenWidth, BackFirst, BackLimit, SpanX);

  // C's division truncates toward zero, as the original's.
  frontX_[0] = wrapOnce(
      frontX_[0] + static_cast<std::int16_t>(deltaX * FrontSpeedNumerator / FrontSpeedDenominator) +
          drift,
      FrontFirst, FrontLimit, SpanX);
  // The second edge is wrapped twice over, as the original does.
  frontX_[1] = wrapOnce(frontX_[0] + ScreenWidth, FrontFirst, FrontLimit, SpanX);
  frontX_[1] = wrapOnce(frontX_[1], FrontFirst, FrontLimit, SpanX);

  rowsY_[0] = wrapOnce(rowsY_[0] + deltaY, RowsFirst, RowsLimit, SpanY);
  rowsY_[1] = wrapOnce(rowsY_[0] + ScreenHeight, RowsFirst, RowsLimit, SpanY);
}

std::optional<assets::Image> mistImage(const assets::MapImages& images, std::int32_t palette) {
  const auto page = std::ranges::find_if(images.pages, [](const assets::MapTexturePage& candidate) {
    return candidate.bitsPerPixel == 4 && candidate.vramX == MistPageX;
  });
  const std::vector<assets::PaletteColor>* colors =
      palette < 0 ? nullptr
                  : assets::mapClutPalette(palette, images.pages, images.backgroundPalettes);
  if (page == images.pages.end() || colors == nullptr ||
      page->image.width < static_cast<std::uint32_t>(MistLayers::TexelsWide + 1) ||
      page->image.height < static_cast<std::uint32_t>(MistLayers::TexelsHigh)) {
    return std::nullopt;
  }
  // The rows the quads read; the page's other rows hold another picture.
  assets::IndexedImage picture{.width = page->image.width,
                               .height = static_cast<std::uint32_t>(MistLayers::TexelsHigh),
                               .indices = {},
                               .palette = {}};
  const auto texels = static_cast<std::size_t>(picture.width) * picture.height;
  picture.indices.assign(page->image.indices.begin(),
                         page->image.indices.begin() + static_cast<std::ptrdiff_t>(texels));
  return assets::applyPalette(picture, *colors);
}

std::vector<ScenePrim> mistPrims(const MistLayers& layers, const MistTextures& textures,
                                 glm::vec2 shift, glm::vec2 areaMin, glm::vec2 areaMax) {
  std::vector<ScenePrim> prims;
  if (areaMax.x <= areaMin.x || areaMax.y <= areaMin.y) {
    return prims;
  }
  constexpr glm::vec2 Screen{MistLayers::ScreenWidth, MistLayers::ScreenHeight};
  // The texels a quad spans: u 0 to 0xff of the page's 256, v 0 to 0xc8 of the picture's 200 rows.
  constexpr glm::vec2 UvSpan{static_cast<float>(MistLayers::TexelsWide) / 256.0F, 1.0F};
  constexpr float Tint = 128.0F;
  // The first left (or top) edge of a repetition at or before `from`, `phase` being one of them.
  const auto firstEdge = [](float phase, float from, float period) {
    return phase + (std::floor((from - phase) / period) * period);
  };
  const auto addLayer = [&](rhi::TextureId texture, float leftPhase, bool mirrored, float tint,
                            float strength) {
    const glm::vec4 color{glm::vec3{tint / Tint}, strength};
    const float top0 =
        firstEdge(static_cast<float>(layers.rowsY()[0]) + shift.y, areaMin.y, Screen.y);
    const float left0 = firstEdge(leftPhase, areaMin.x, Screen.x);
    for (float top = top0; top < areaMax.y; top += Screen.y) {
      for (float left = left0; left < areaMax.x; left += Screen.x) {
        // The quad cut to the area, with its texels cut alike.
        const glm::vec2 from = glm::max(glm::vec2{left, top}, areaMin);
        const glm::vec2 to = glm::min(glm::vec2{left, top} + Screen, areaMax);
        if (to.x <= from.x || to.y <= from.y) {
          continue;
        }
        glm::vec2 uvFrom = (from - glm::vec2{left, top}) / Screen * UvSpan;
        glm::vec2 uvTo = (to - glm::vec2{left, top}) / Screen * UvSpan;
        if (mirrored) {
          // The front layer's left corners read u 0xff and its right ones u 0.
          uvFrom.x = UvSpan.x - uvFrom.x;
          uvTo.x = UvSpan.x - uvTo.x;
        }
        ScenePrim prim{.shape = ScenePrim::Shape::Quad,
                       .color = color,
                       .texture = texture,
                       .blend = ScenePrimBlend::Additive,
                       .stpMask = true};
        const glm::vec4 centre{0.0F, 0.0F, 0.0F, 1.0F};
        prim.corners[0] = {.clip = centre, .offset = from, .uv = uvFrom};
        prim.corners[1] = {.clip = centre, .offset = {to.x, from.y}, .uv = {uvTo.x, uvFrom.y}};
        prim.corners[2] = {.clip = centre, .offset = {from.x, to.y}, .uv = {uvFrom.x, uvTo.y}};
        prim.corners[3] = {.clip = centre, .offset = to, .uv = uvTo};
        prims.push_back(prim);
      }
    }
  };
  // B + F at 0x50 for the front layer, B + F / 4 at 0x96 for the back one (GetTPage modes 1 and 3).
  constexpr float Whole = 1.0F;
  constexpr float Quarter = 0.25F;
  if (textures.front) {
    const float right = static_cast<float>(layers.frontX()[0]) + shift.x;
    addLayer(*textures.front, right - Screen.x, true, MistLayers::FrontTint, Whole);
  }
  if (textures.back) {
    addLayer(*textures.back, static_cast<float>(layers.backX()[0]) + shift.x, false,
             MistLayers::BackTint, Quarter);
  }
  return prims;
}

} // namespace dwe::app
