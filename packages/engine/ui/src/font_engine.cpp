#include "font_engine.hpp"

#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <iterator>
#include <string>
#include <utility>

namespace dwe::ui {
namespace {

// The underline is one pixel of the bitmap font thick, one pixel under the baseline.
constexpr int UnderlinePixels = 1;

// The lowercase letter whose height is the font's x-height.
constexpr char32_t XHeightLetter = U'x';

// How much past a whole multiple the bitmap font's scale may fall and still round down to it: a
// window a few pixels smaller than the design height draws at the design's scale.
constexpr double ScaleSlack = 0.1;

// How many times larger than its atlas the bitmap font draws at a font size, with `dpRatio`
// pixels per dp: a whole multiple, so each atlas pixel is a square of screen pixels and stays
// crisp. At the design height (one pixel per dp) it is the multiple nearest to the size over the
// cell height, as the boxes were laid out for; at any other height that multiple grows with the
// window, rounded down, so the text never grows faster than its box. Rounding the size at each
// height instead drew texts up to half a cell larger than at the design height and pushed them
// out of their boxes (at 1920x1080, 36dp is 54 pixels: 5 times the 11-pixel cell where the boxes
// grew 4.5 times).
int bitmapScale(int size, int cellHeight, float dpRatio) {
  const double ratio = dpRatio > 0.0F ? dpRatio : 1.0;
  const long design = std::max(1L, std::lround(static_cast<double>(size) / ratio / cellHeight));
  return std::max(1, static_cast<int>(static_cast<double>(design) * ratio + ScaleSlack));
}

Rml::Span<const Rml::byte> bytesOf(const assets::Image& image) {
  return {reinterpret_cast<const Rml::byte*>(image.pixels.data()), image.pixels.size()};
}

} // namespace

FontEngine::FontEngine(Rml::FontEngineInterface& freeType, OriginalLook look,
                       std::optional<BitmapFont> bitmap)
    : freeType_{&freeType}, look_{std::move(look)}, bitmap_{std::move(bitmap)},
      textFont_{look_.defaultFont} {
  // RmlUi hands families to the engine in lowercase, as RCSS compares them, and its own engine
  // expects them so.
  look_.family = Rml::StringUtilities::ToLower(look_.family);
  look_.modernFamily = Rml::StringUtilities::ToLower(look_.modernFamily);
  look_.dialogueFamily = Rml::StringUtilities::ToLower(look_.dialogueFamily);
}

void FontEngine::Initialize() {
  freeType_->Initialize();
}

void FontEngine::Shutdown() {
  faces_.clear();
  freeType_->Shutdown();
}

bool FontEngine::LoadFontFace(const Rml::String& fileName, int faceIndex, bool fallbackFace,
                              Rml::Style::FontWeight weight) {
  return freeType_->LoadFontFace(fileName, faceIndex, fallbackFace, weight);
}

bool FontEngine::LoadFontFace(const Rml::String& fileName, int faceIndex, const Rml::String& family,
                              Rml::Style::FontStyle style, Rml::Style::FontWeight weight,
                              bool fallbackFace) {
  return freeType_->LoadFontFace(fileName, faceIndex, family, style, weight, fallbackFace);
}

bool FontEngine::LoadFontFace(Rml::Span<const Rml::byte> data, int faceIndex,
                              const Rml::String& family, Rml::Style::FontStyle style,
                              Rml::Style::FontWeight weight, bool fallbackFace) {
  return freeType_->LoadFontFace(data, faceIndex, family, style, weight, fallbackFace);
}

Rml::FontFaceHandle FontEngine::GetFontFaceHandle(const Rml::String& family,
                                                  Rml::Style::FontStyle style,
                                                  Rml::Style::FontWeight weight, int size) {
  if (!isOriginalLook(family)) {
    return freeType_->GetFontFaceHandle(family, style, weight, size);
  }
  if (textFont_ == TextFont::Original && bitmap_) {
    const bool dialogue = family == look_.dialogueFamily;
    return bitmapFaceHandle(*bitmap_, dialogue, style, weight, size);
  }
  return freeType_->GetFontFaceHandle(look_.modernFamily, style, weight, size);
}

Rml::FontEffectsHandle FontEngine::PrepareFontEffects(Rml::FontFaceHandle handle,
                                                      const Rml::FontEffectList& fontEffects) {
  // The effects (shadows, outlines) apply to what the modern face draws; the bitmap glyphs have
  // none.
  const BitmapFace* face = bitmapFace(handle);
  return freeType_->PrepareFontEffects(face != nullptr ? face->fallback : handle, fontEffects);
}

const Rml::FontMetrics& FontEngine::GetFontMetrics(Rml::FontFaceHandle handle) {
  if (const BitmapFace* face = bitmapFace(handle)) {
    return face->metrics;
  }
  return freeType_->GetFontMetrics(handle);
}

int FontEngine::GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView given,
                               const Rml::TextShapingContext& shaping,
                               Rml::Character priorCharacter) {
  const std::string capitals = finishUppercase({given.begin(), given.size()});
  const Rml::StringView string{capitals.data(), capitals.data() + capitals.size()};
  const BitmapFace* face = bitmapFace(handle);
  if (face == nullptr) {
    return freeType_->GetStringWidth(handle, string, shaping, priorCharacter);
  }
  // RmlUi measures a line a word at a time, with the character before each word.
  const BitmapGlyph* prior = face->font->glyph(static_cast<char32_t>(priorCharacter));
  int previousAdvance =
      prior != nullptr && !isSpace(static_cast<char32_t>(priorCharacter)) ? prior->advance : 0;
  float width = 0.0F;
  for (const TextRun& run : face->font->splitRuns({string.begin(), string.size()})) {
    const Rml::StringView text{run.text.data(), run.text.data() + run.text.size()};
    if (run.bitmap) {
      width += bitmapRunWidth(*face, text, shaping.letter_spacing, previousAdvance);
      continue;
    }
    width += static_cast<float>(
        freeType_->GetStringWidth(face->fallback, text, shaping, Rml::Character::Null));
    previousAdvance = 0;
  }
  return static_cast<int>(std::lround(width));
}

int FontEngine::GenerateString(Rml::RenderManager& renderManager, Rml::FontFaceHandle faceHandle,
                               Rml::FontEffectsHandle effectsHandle, Rml::StringView given,
                               Rml::Vector2f position, Rml::ColourbPremultiplied colour,
                               float opacity, const Rml::TextShapingContext& shaping,
                               Rml::TexturedMeshList& meshList) {
  // RmlUi's capitals leave accented letters small (text_font.hpp: finishUppercase).
  const std::string capitals = finishUppercase({given.begin(), given.size()});
  const Rml::StringView string{capitals.data(), capitals.data() + capitals.size()};
  const BitmapFace* face = bitmapFace(faceHandle);
  if (face == nullptr) {
    return freeType_->GenerateString(renderManager, faceHandle, effectsHandle, string, position,
                                     colour, opacity, shaping, meshList);
  }
  // Whole pixels, so every atlas pixel covers whole screen pixels.
  const float startX = std::round(position.x);
  const float baseline = std::round(position.y);
  float penX = startX;
  int previousAdvance = 0;
  Rml::TexturedMesh glyphs{.mesh = {}, .texture = face->texture.GetTexture(renderManager)};
  for (const TextRun& run : face->font->splitRuns({string.begin(), string.size()})) {
    const Rml::StringView text{run.text.data(), run.text.data() + run.text.size()};
    if (run.bitmap) {
      addBitmapRun(*face, text, colour * opacity, startX, baseline, shaping.letter_spacing, penX,
                   previousAdvance, glyphs.mesh);
      continue;
    }
    previousAdvance = 0;
    Rml::TexturedMeshList fallbackMeshes;
    penX += static_cast<float>(freeType_->GenerateString(renderManager, face->fallback,
                                                         effectsHandle, text, {penX, baseline},
                                                         colour, opacity, shaping, fallbackMeshes));
    std::ranges::move(fallbackMeshes, std::back_inserter(meshList));
  }
  if (glyphs.mesh) {
    meshList.push_back(std::move(glyphs));
  }
  return static_cast<int>(std::lround(penX - startX));
}

int FontEngine::GetVersion(Rml::FontFaceHandle handle) {
  // The modern face adds glyphs to its textures as text asks for them; the bitmap atlas never
  // changes.
  const BitmapFace* face = bitmapFace(handle);
  return freeType_->GetVersion(face != nullptr ? face->fallback : handle);
}

void FontEngine::ReleaseFontResources() {
  faces_.clear();
  freeType_->ReleaseFontResources();
}

bool FontEngine::isOriginalLook(const Rml::String& family) const {
  return family == look_.family ||
         (!look_.dialogueFamily.empty() && family == look_.dialogueFamily);
}

Rml::FontFaceHandle FontEngine::bitmapFaceHandle(const BitmapFont& font, bool dialogue,
                                                 Rml::Style::FontStyle style,
                                                 Rml::Style::FontWeight weight, int size) {
  const int scale = bitmapScale(size, font.cellHeight(), dpRatio_);
  const auto same = [&](const std::unique_ptr<BitmapFace>& face) {
    return face->dialogue == dialogue && face->style == style && face->weight == weight &&
           face->size == size && face->scale == scale;
  };
  if (auto found = std::ranges::find_if(faces_, same); found != faces_.end()) {
    return std::bit_cast<Rml::FontFaceHandle>(found->get());
  }
  const int fallbackSize =
      std::max(1, static_cast<int>(std::lround(static_cast<float>(font.cellHeight() * scale) *
                                               look_.fallbackSize)));
  const Rml::FontFaceHandle fallback =
      freeType_->GetFontFaceHandle(look_.modernFamily, style, weight, fallbackSize);
  if (fallback == 0) {
    return 0;
  }
  assets::Image atlas = font.inkAtlas(scale);
  const Rml::Vector2i atlasSize{static_cast<int>(atlas.width), static_cast<int>(atlas.height)};
  auto face = std::make_unique<BitmapFace>(BitmapFace{
      .font = &font,
      .dialogue = dialogue,
      .style = style,
      .weight = weight,
      .size = size,
      .scale = scale,
      .fallback = fallback,
      .metrics = bitmapMetrics(font, size, scale),
      .atlasSize = Rml::Vector2f{atlasSize},
      .texture = Rml::CallbackTextureSource{[atlas = std::move(atlas), atlasSize](
                                                const Rml::CallbackTextureInterface& texture) {
        return texture.GenerateTexture(bytesOf(atlas), atlasSize);
      }},
  });
  faces_.push_back(std::move(face));
  return std::bit_cast<Rml::FontFaceHandle>(faces_.back().get());
}

Rml::FontMetrics FontEngine::bitmapMetrics(const BitmapFont& font, int size, int scale) {
  const int baseline = font.baseline();
  const BitmapGlyph* letter = font.glyph(XHeightLetter);
  const int xHeight = letter != nullptr ? baseline - font.inkTop(*letter) : baseline;
  return Rml::FontMetrics{
      .size = size,
      .ascent = static_cast<float>(baseline * scale),
      .descent = static_cast<float>((font.cellHeight() - baseline) * scale),
      .line_spacing = static_cast<float>(font.cellHeight() * scale),
      .x_height = static_cast<float>(xHeight * scale),
      .underline_position = static_cast<float>(UnderlinePixels * scale),
      .underline_thickness = static_cast<float>(UnderlinePixels * scale),
      .has_ellipsis = false,
  };
}

FontEngine::BitmapFace* FontEngine::bitmapFace(Rml::FontFaceHandle handle) const {
  auto found = std::ranges::find_if(faces_, [handle](const auto& face) {
    return std::bit_cast<Rml::FontFaceHandle>(face.get()) == handle;
  });
  return found == faces_.end() ? nullptr : found->get();
}

float FontEngine::bitmapRunWidth(const BitmapFace& face, Rml::StringView run, float letterSpacing,
                                 int& previousAdvance) {
  float width = 0.0F;
  for (Rml::StringIteratorU8 it{run}; it; ++it) {
    const auto character = static_cast<char32_t>(*it);
    const int advance =
        glyphAdvance(*face.font->glyph(character), character, face.dialogue, previousAdvance);
    width += static_cast<float>(advance * face.scale) + letterSpacing;
  }
  return width;
}

void FontEngine::addBitmapRun(const BitmapFace& face, Rml::StringView run,
                              Rml::ColourbPremultiplied tint, float startX, float baseline,
                              float letterSpacing, float& penX, int& previousAdvance,
                              Rml::Mesh& mesh) {
  const auto cellWidth = static_cast<float>(face.font->cellWidth() * face.scale);
  const auto cellHeight = static_cast<float>(face.font->cellHeight() * face.scale);
  const float top = baseline - face.metrics.ascent;
  for (Rml::StringIteratorU8 it{run}; it; ++it) {
    const auto character = static_cast<char32_t>(*it);
    const BitmapGlyph& glyph = *face.font->glyph(character);
    const int advance = glyphAdvance(glyph, character, face.dialogue, previousAdvance);
    if (!isSpace(character)) {
      const float x = startX + onGlyphGrid(penX - startX, face.scale);
      const Rml::Vector2f cell{static_cast<float>(glyph.x * face.scale),
                               static_cast<float>(glyph.y * face.scale)};
      Rml::MeshUtilities::GenerateQuad(
          mesh, {std::round(x), top}, {cellWidth, cellHeight}, tint, cell / face.atlasSize,
          (cell + Rml::Vector2f{cellWidth, cellHeight}) / face.atlasSize);
    }
    penX += static_cast<float>(advance * face.scale) + letterSpacing;
  }
}

} // namespace dwe::ui
