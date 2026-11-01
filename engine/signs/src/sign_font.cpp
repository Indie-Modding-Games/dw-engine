#include <dwe/signs/sign_font.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <climits>
#include <cmath>
#include <format>
#include <utility>

namespace dwe::signs {
namespace {

// FreeType's 26.6 fixed point.
constexpr float FixedOne = 64.0F;
constexpr char32_t Replacement = 0xFFFD;

// A glyph's bitmap placed on the line.
struct PlacedGlyph {
  std::int32_t left = 0;
  std::int32_t top = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::vector<float> values;
};

} // namespace

struct SignFont::Face {
  std::vector<std::byte> file;
  FT_Library library = nullptr;
  FT_Face face = nullptr;

  Face() = default;
  Face(const Face&) = delete;
  Face& operator=(const Face&) = delete;
  Face(Face&&) = delete;
  Face& operator=(Face&&) = delete;
  ~Face() {
    if (face != nullptr) {
      FT_Done_Face(face);
    }
    if (library != nullptr) {
      FT_Done_FreeType(library);
    }
  }

  [[nodiscard]] bool setSize(float pixelSize) const {
    const auto size = static_cast<FT_F26Dot6>(std::lround(pixelSize * FixedOne));
    return FT_Set_Char_Size(face, 0, std::max<FT_F26Dot6>(size, 1), 72, 72) == 0;
  }
};

SignFont::SignFont(std::unique_ptr<Face> face) : face_(std::move(face)) {}
SignFont::SignFont(SignFont&&) noexcept = default;
SignFont& SignFont::operator=(SignFont&&) noexcept = default;
SignFont::~SignFont() = default;

core::Result<SignFont> SignFont::load(std::vector<std::byte> fontFile) {
  auto face = std::make_unique<Face>();
  face->file = std::move(fontFile);
  if (const FT_Error error = FT_Init_FreeType(&face->library); error != 0) {
    return core::fail(std::format("FreeType does not start (error {})", error));
  }
  if (const FT_Error error =
          FT_New_Memory_Face(face->library, reinterpret_cast<const FT_Byte*>(face->file.data()),
                             static_cast<FT_Long>(face->file.size()), 0, &face->face);
      error != 0) {
    face->face = nullptr;
    return core::fail(std::format("not a font FreeType reads (error {})", error));
  }
  if (!FT_IS_SCALABLE(face->face)) {
    return core::fail("the font has no outlines");
  }
  return SignFont{std::move(face)};
}

float SignFont::lineHeight(float pixelSize) const {
  if (!face_->setSize(pixelSize)) {
    return pixelSize;
  }
  return static_cast<float>(face_->face->size->metrics.height) / FixedOne;
}

TextLine SignFont::drawLine(std::string_view text, float pixelSize, float letterSpacing) const {
  TextLine line;
  if (!face_->setSize(pixelSize)) {
    return line;
  }
  FT_Face face = face_->face;
  std::vector<PlacedGlyph> glyphs;
  float pen = 0.0F;
  FT_UInt previous = 0;
  const bool kerning = FT_HAS_KERNING(face);
  for (const char32_t codePoint : decodeUtf8(text)) {
    const FT_UInt glyphIndex = FT_Get_Char_Index(face, codePoint);
    if (glyphIndex == 0) {
      continue;
    }
    if (kerning && previous != 0) {
      FT_Vector delta{};
      if (FT_Get_Kerning(face, previous, glyphIndex, FT_KERNING_UNFITTED, &delta) == 0) {
        pen += static_cast<float>(delta.x) / FixedOne;
      }
    }
    previous = glyphIndex;
    if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER | FT_LOAD_NO_HINTING) != 0) {
      continue;
    }
    const FT_GlyphSlot slot = face->glyph;
    const FT_Bitmap& bitmap = slot->bitmap;
    if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY && bitmap.width > 0 && bitmap.rows > 0) {
      PlacedGlyph placed{
          .left = static_cast<std::int32_t>(std::lround(pen)) + slot->bitmap_left,
          .top = -slot->bitmap_top,
          .width = static_cast<std::int32_t>(bitmap.width),
          .height = static_cast<std::int32_t>(bitmap.rows),
          .values = {},
      };
      placed.values.resize(static_cast<std::size_t>(placed.width) *
                           static_cast<std::size_t>(placed.height));
      const float levels = static_cast<float>(std::max(bitmap.num_grays - 1, 1));
      for (std::int32_t row = 0; row < placed.height; ++row) {
        const unsigned char* source =
            bitmap.buffer + (static_cast<std::ptrdiff_t>(row) * bitmap.pitch);
        for (std::int32_t column = 0; column < placed.width; ++column) {
          placed.values[(static_cast<std::size_t>(row) * static_cast<std::size_t>(placed.width)) +
                        static_cast<std::size_t>(column)] =
              static_cast<float>(source[column]) / levels;
        }
      }
      glyphs.push_back(std::move(placed));
    }
    pen += (static_cast<float>(slot->advance.x) / FixedOne) + letterSpacing;
  }
  line.advance = std::max(pen - letterSpacing, 0.0F);
  if (glyphs.empty()) {
    return line;
  }
  std::int32_t left = INT_MAX;
  std::int32_t top = INT_MAX;
  std::int32_t right = INT_MIN;
  std::int32_t bottom = INT_MIN;
  for (const PlacedGlyph& glyph : glyphs) {
    left = std::min(left, glyph.left);
    top = std::min(top, glyph.top);
    right = std::max(right, glyph.left + glyph.width);
    bottom = std::max(bottom, glyph.top + glyph.height);
  }
  line.ink.width = right - left;
  line.ink.height = bottom - top;
  line.ink.values.assign(
      static_cast<std::size_t>(line.ink.width) * static_cast<std::size_t>(line.ink.height), 0.0F);
  for (const PlacedGlyph& glyph : glyphs) {
    for (std::int32_t row = 0; row < glyph.height; ++row) {
      for (std::int32_t column = 0; column < glyph.width; ++column) {
        const std::int32_t inkX = glyph.left - left + column;
        const std::int32_t inkY = glyph.top - top + row;
        const auto x = static_cast<std::size_t>(inkX);
        const auto y = static_cast<std::size_t>(inkY);
        float& value = line.ink.values[(y * static_cast<std::size_t>(line.ink.width)) + x];
        value = std::min(1.0F, value + glyph.values[(static_cast<std::size_t>(row) *
                                                     static_cast<std::size_t>(glyph.width)) +
                                                    static_cast<std::size_t>(column)]);
      }
    }
  }
  line.originX = static_cast<float>(-left);
  line.baseline = static_cast<float>(-top);
  return line;
}

std::vector<char32_t> decodeUtf8(std::string_view text) {
  std::vector<char32_t> codePoints;
  std::size_t at = 0;
  while (at < text.size()) {
    const auto lead = static_cast<unsigned char>(text[at]);
    std::size_t length = 0;
    char32_t value = 0;
    if (lead < 0x80U) {
      length = 1;
      value = lead;
    } else if ((lead & 0xE0U) == 0xC0U) {
      length = 2;
      value = lead & 0x1FU;
    } else if ((lead & 0xF0U) == 0xE0U) {
      length = 3;
      value = lead & 0x0FU;
    } else if ((lead & 0xF8U) == 0xF0U) {
      length = 4;
      value = lead & 0x07U;
    }
    bool valid = length != 0 && at + length <= text.size();
    for (std::size_t next = 1; valid && next < length; ++next) {
      const auto byte = static_cast<unsigned char>(text[at + next]);
      valid = (byte & 0xC0U) == 0x80U;
      value = (value << 6U) | (byte & 0x3FU);
    }
    if (!valid) {
      codePoints.push_back(Replacement);
      ++at;
      continue;
    }
    codePoints.push_back(value);
    at += length;
  }
  return codePoints;
}

} // namespace dwe::signs
