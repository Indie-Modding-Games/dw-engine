#include "font_engine.hpp"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/StringUtilities.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <bit>
#include <cmath>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

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

bool staysInRun(char32_t character) {
  constexpr char32_t FirstPrintable = 0x20;
  constexpr char32_t ZeroWidthSpace = 0x200B;
  return character <= FirstPrintable || character == ZeroWidthSpace || character == U'\u00A0' ||
         character == U'\u3000';
}

bool hasCharacter(const std::vector<char32_t>* coverage, char32_t character) {
  return coverage != nullptr && std::ranges::binary_search(*coverage, character);
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
  look_.fallbackFamily = Rml::StringUtilities::ToLower(look_.fallbackFamily);
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
  if (!freeType_->LoadFontFace(fileName, faceIndex, fallbackFace, weight)) {
    return false;
  }
  recordFileCoverage(fileName, faceIndex, {});
  return true;
}

bool FontEngine::LoadFontFace(const Rml::String& fileName, int faceIndex, const Rml::String& family,
                              Rml::Style::FontStyle style, Rml::Style::FontWeight weight,
                              bool fallbackFace) {
  if (!freeType_->LoadFontFace(fileName, faceIndex, family, style, weight, fallbackFace)) {
    return false;
  }
  recordFileCoverage(fileName, faceIndex, family);
  return true;
}

bool FontEngine::LoadFontFace(Rml::Span<const Rml::byte> data, int faceIndex,
                              const Rml::String& family, Rml::Style::FontStyle style,
                              Rml::Style::FontWeight weight, bool fallbackFace) {
  if (!freeType_->LoadFontFace(data, faceIndex, family, style, weight, fallbackFace)) {
    return false;
  }
  recordCoverage(data, faceIndex, family);
  return true;
}

void FontEngine::setFallbacks(std::vector<FallbackChain> chains) {
  for (FallbackChain& chain : chains) {
    for (std::string& family : chain.families) {
      family = Rml::StringUtilities::ToLower(family);
    }
    for (FallbackFace& face : chain.faces) {
      face.family = Rml::StringUtilities::ToLower(face.family);
    }
  }
  chains_ = std::move(chains);
  chainFaces_.clear();
}

bool FontEngine::setLanguage(std::string_view language) {
  if (language == language_) {
    return false;
  }
  language_ = language;
  return !chains_.empty();
}

bool FontEngine::covers(std::string_view text) const {
  for (Rml::StringIteratorU8 it{Rml::StringView{text.data(), text.data() + text.size()}}; it;
       ++it) {
    const auto character = static_cast<char32_t>(*it);
    if (staysInRun(character)) {
      continue;
    }
    const bool found = std::ranges::any_of(coverage_, [character](const auto& family) {
      return hasCharacter(&family.second, character);
    });
    if (!found) {
      return false;
    }
  }
  return true;
}

void FontEngine::recordFileCoverage(const Rml::String& fileName, int faceIndex,
                                    const Rml::String& family) {
  Rml::FileInterface* files = Rml::GetFileInterface();
  if (files == nullptr) {
    return;
  }
  const Rml::FileHandle file = files->Open(fileName);
  if (file == 0) {
    return;
  }
  std::vector<Rml::byte> bytes(files->Length(file));
  const std::size_t read = files->Read(bytes.data(), bytes.size(), file);
  files->Close(file);
  bytes.resize(read);
  recordCoverage({bytes.data(), bytes.size()}, faceIndex, family);
}

void FontEngine::recordCoverage(Rml::Span<const Rml::byte> data, int faceIndex,
                                const Rml::String& family) {
  FT_Library library = nullptr;
  if (FT_Init_FreeType(&library) != 0) {
    return;
  }
  FT_Face face = nullptr;
  if (FT_New_Memory_Face(library, data.data(), static_cast<FT_Long>(data.size()), faceIndex,
                         &face) == 0) {
    const Rml::String name = Rml::StringUtilities::ToLower(
        !family.empty() ? family
                        : Rml::String{face->family_name != nullptr ? face->family_name : ""});
    Coverage& coverage = coverage_[name];
    FT_UInt glyph = 0;
    for (FT_ULong code = FT_Get_First_Char(face, &glyph); glyph != 0;
         code = FT_Get_Next_Char(face, code, &glyph)) {
      coverage.push_back(static_cast<char32_t>(code));
    }
    std::ranges::sort(coverage);
    const auto repeated = std::ranges::unique(coverage);
    coverage.erase(repeated.begin(), repeated.end());
    FT_Done_Face(face);
  }
  FT_Done_FreeType(library);
}

const FontEngine::Coverage* FontEngine::coverageOf(const Rml::String& family) const {
  const auto found = coverage_.find(family);
  return found != coverage_.end() ? &found->second : nullptr;
}

const FallbackChain* FontEngine::chainOf(const Rml::String& family) const {
  const auto found = std::ranges::find_if(chains_, [&family](const FallbackChain& chain) {
    return std::ranges::contains(chain.families, family);
  });
  return found != chains_.end() ? &*found : nullptr;
}

Rml::FontFaceHandle FontEngine::chainFaceHandle(const FallbackChain& chain,
                                                const Rml::String& family,
                                                Rml::Style::FontStyle style,
                                                Rml::Style::FontWeight weight, int size) {
  const auto same = [&](const std::unique_ptr<ChainFace>& face) {
    return face->family == family && face->style == style && face->weight == weight &&
           face->size == size;
  };
  if (auto found = std::ranges::find_if(chainFaces_, same); found != chainFaces_.end()) {
    return std::bit_cast<Rml::FontFaceHandle>(found->get());
  }
  const Rml::FontFaceHandle primary = freeType_->GetFontFaceHandle(family, style, weight, size);
  if (primary == 0) {
    return 0;
  }
  auto face = std::make_unique<ChainFace>(ChainFace{
      .family = family,
      .style = style,
      .weight = weight,
      .size = size,
      .primary = primary,
      .primaryCoverage = coverageOf(family),
      .fallbacks = {},
      .coverages = {},
      .effects = {},
  });
  for (const FallbackFace& fallback : fallbackOrder(chain.faces, language_)) {
    if (fallback.family == family) {
      continue;
    }
    const int fallbackSize =
        std::max(1, static_cast<int>(std::lround(static_cast<float>(size) * fallback.scale)));
    const Rml::FontFaceHandle handle =
        freeType_->GetFontFaceHandle(fallback.family, style, weight, fallbackSize);
    if (handle != 0) {
      face->fallbacks.push_back(handle);
      face->coverages.push_back(coverageOf(fallback.family));
    }
  }
  chainFaces_.push_back(std::move(face));
  return std::bit_cast<Rml::FontFaceHandle>(chainFaces_.back().get());
}

FontEngine::ChainFace* FontEngine::chainFace(Rml::FontFaceHandle handle) const {
  auto found = std::ranges::find_if(chainFaces_, [handle](const auto& face) {
    return std::bit_cast<Rml::FontFaceHandle>(face.get()) == handle;
  });
  return found == chainFaces_.end() ? nullptr : found->get();
}

std::vector<FontEngine::ChainRun> FontEngine::chainRuns(const ChainFace& face,
                                                        Rml::StringView string) {
  std::vector<ChainRun> runs;
  const char* runStart = string.begin();
  std::size_t current = 0;
  for (Rml::StringIteratorU8 it{string}; it; ++it) {
    const auto character = static_cast<char32_t>(*it);
    std::size_t wanted = current;
    if (staysInRun(character)) {
      if (current != 0 && !hasCharacter(face.coverages[current - 1], character)) {
        wanted = 0;
      }
    } else {
      wanted = 0;
      if (face.primaryCoverage != nullptr && !hasCharacter(face.primaryCoverage, character)) {
        for (std::size_t i = 0; i < face.fallbacks.size(); ++i) {
          if (hasCharacter(face.coverages[i], character)) {
            wanted = i + 1;
            break;
          }
        }
      }
    }
    if (wanted != current) {
      if (it.get() != runStart) {
        runs.push_back({.face = current, .text = Rml::StringView{runStart, it.get()}});
      }
      runStart = it.get();
      current = wanted;
    }
  }
  if (runStart != string.end()) {
    runs.push_back({.face = current, .text = Rml::StringView{runStart, string.end()}});
  }
  return runs;
}

Rml::FontFaceHandle FontEngine::runHandle(const ChainFace& face, std::size_t run) const {
  return run == 0 ? face.primary : face.fallbacks[run - 1];
}

Rml::FontFaceHandle FontEngine::GetFontFaceHandle(const Rml::String& family,
                                                  Rml::Style::FontStyle style,
                                                  Rml::Style::FontWeight weight, int size) {
  if (!isOriginalLook(family)) {
    if (const FallbackChain* chain = chainOf(family)) {
      return chainFaceHandle(*chain, family, style, weight, size);
    }
    return freeType_->GetFontFaceHandle(family, style, weight, size);
  }
  if (textFont_ == TextFont::Original && bitmap_) {
    const bool dialogue = family == look_.dialogueFamily;
    return bitmapFaceHandle(*bitmap_, dialogue, style, weight, size);
  }
  return GetFontFaceHandle(look_.modernFamily, style, weight, size);
}

Rml::FontEffectsHandle FontEngine::PrepareFontEffects(Rml::FontFaceHandle handle,
                                                      const Rml::FontEffectList& fontEffects) {
  // The effects (shadows, outlines) apply to what the modern face draws; the bitmap glyphs have
  // none.
  if (const BitmapFace* face = bitmapFace(handle)) {
    return PrepareFontEffects(face->fallback, fontEffects);
  }
  if (ChainFace* chain = chainFace(handle)) {
    const Rml::FontEffectsHandle own = freeType_->PrepareFontEffects(chain->primary, fontEffects);
    std::vector<Rml::FontEffectsHandle> fallbacks;
    fallbacks.reserve(chain->fallbacks.size());
    for (const Rml::FontFaceHandle fallback : chain->fallbacks) {
      fallbacks.push_back(freeType_->PrepareFontEffects(fallback, fontEffects));
    }
    chain->effects[own] = std::move(fallbacks);
    return own;
  }
  return freeType_->PrepareFontEffects(handle, fontEffects);
}

const Rml::FontMetrics& FontEngine::GetFontMetrics(Rml::FontFaceHandle handle) {
  if (const BitmapFace* face = bitmapFace(handle)) {
    return face->metrics;
  }
  if (const ChainFace* chain = chainFace(handle)) {
    return freeType_->GetFontMetrics(chain->primary);
  }
  return freeType_->GetFontMetrics(handle);
}

int FontEngine::GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView given,
                               const Rml::TextShapingContext& shaping,
                               Rml::Character priorCharacter) {
  const std::string capitals = finishUppercase({given.begin(), given.size()});
  const Rml::StringView string{capitals.data(), capitals.data() + capitals.size()};
  if (const ChainFace* chain = chainFace(handle)) {
    int width = 0;
    bool first = true;
    for (const ChainRun& run : chainRuns(*chain, string)) {
      width += freeType_->GetStringWidth(runHandle(*chain, run.face), run.text, shaping,
                                         first ? priorCharacter : Rml::Character::Null);
      first = false;
    }
    return width;
  }
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
    width +=
        static_cast<float>(GetStringWidth(face->fallback, text, shaping, Rml::Character::Null));
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
  if (const ChainFace* chain = chainFace(faceHandle)) {
    const auto effects = chain->effects.find(effectsHandle);
    float penX = position.x;
    for (const ChainRun& run : chainRuns(*chain, string)) {
      Rml::FontEffectsHandle runEffects = effectsHandle;
      if (run.face != 0) {
        runEffects = effects != chain->effects.end() ? effects->second[run.face - 1] : 0;
      }
      // RmlUi's GenerateString resizes the list it is given, so each run gets its own.
      Rml::TexturedMeshList runMeshes;
      penX += static_cast<float>(freeType_->GenerateString(
          renderManager, runHandle(*chain, run.face), runEffects, run.text, {penX, position.y},
          colour, opacity, shaping, runMeshes));
      std::ranges::move(runMeshes, std::back_inserter(meshList));
    }
    return static_cast<int>(std::lround(penX - position.x));
  }
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
      addBitmapRun(*face, text, colour * opacity, baseline, shaping.letter_spacing, penX,
                   previousAdvance, glyphs.mesh);
      continue;
    }
    previousAdvance = 0;
    Rml::TexturedMeshList fallbackMeshes;
    penX += static_cast<float>(GenerateString(renderManager, face->fallback, effectsHandle, text,
                                              {penX, baseline}, colour, opacity, shaping,
                                              fallbackMeshes));
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
  if (const BitmapFace* face = bitmapFace(handle)) {
    return GetVersion(face->fallback);
  }
  if (const ChainFace* chain = chainFace(handle)) {
    int version = freeType_->GetVersion(chain->primary);
    for (const Rml::FontFaceHandle fallback : chain->fallbacks) {
      version += freeType_->GetVersion(fallback);
    }
    return version;
  }
  return freeType_->GetVersion(handle);
}

void FontEngine::ReleaseFontResources() {
  faces_.clear();
  chainFaces_.clear();
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
  const bool pixelFallback = !look_.fallbackFamily.empty() && look_.fallbackPixels > 0;
  const int fallbackSize =
      pixelFallback
          ? look_.fallbackPixels * scale
          : std::max(1, static_cast<int>(std::lround(static_cast<float>(font.cellHeight() * scale) *
                                                     look_.fallbackSize)));
  const Rml::FontFaceHandle fallback =
      GetFontFaceHandle(look_.fallbackFamily.empty() ? look_.modernFamily : look_.fallbackFamily,
                        style, weight, fallbackSize);
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
                              Rml::ColourbPremultiplied tint, float baseline, float letterSpacing,
                              float& penX, int& previousAdvance, Rml::Mesh& mesh) {
  const auto cellWidth = static_cast<float>(face.font->cellWidth() * face.scale);
  const auto cellHeight = static_cast<float>(face.font->cellHeight() * face.scale);
  const float top = baseline - face.metrics.ascent;
  // The original's text is all in its own font, so its grid starts with the text. After a glyph
  // the modern font draws (a parenthesis) the grid starts again at the pen: counted from the
  // text's start, the glyph after a space could land up to a grid step back and swallow it.
  const float gridStart = penX;
  for (Rml::StringIteratorU8 it{run}; it; ++it) {
    const auto character = static_cast<char32_t>(*it);
    const BitmapGlyph& glyph = *face.font->glyph(character);
    const int advance = glyphAdvance(glyph, character, face.dialogue, previousAdvance);
    if (!isSpace(character)) {
      const float x = gridStart + onGlyphGrid(penX - gridStart, face.scale);
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
