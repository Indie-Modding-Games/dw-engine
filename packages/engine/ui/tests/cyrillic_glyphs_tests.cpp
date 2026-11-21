#include <doctest/doctest.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::ui {
namespace {

constexpr FT_UInt UnitsPerPixel = 64;

struct Glyph {
  int left = 0;
  int top = 0;
  int width = 0;
  int rows = 0;
  std::vector<bool> pixels;

  [[nodiscard]] bool at(int x, int y) const {
    return pixels[static_cast<std::size_t>((y * width) + x)];
  }

  bool operator==(const Glyph&) const = default;
};

std::string readFile(const std::filesystem::path& path) {
  std::ifstream stream{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

std::filesystem::path fontPath(std::string_view file) {
  return std::filesystem::path{DWE_GAME_PACK} / "ui/fonts" / file;
}

std::vector<char32_t> cyrillicLetters() {
  std::vector<char32_t> letters;
  for (char32_t code = U'А'; code <= U'я'; ++code) {
    letters.push_back(code);
  }
  letters.push_back(U'Ё');
  letters.push_back(U'ё');
  return letters;
}

std::string utf8(char32_t code) {
  std::string out;
  out += static_cast<char>(0xC0 | (code >> 6));
  out += static_cast<char>(0x80 | (code & 0x3F));
  return out;
}

class Face {
public:
  explicit Face(std::string_view file) : bytes_{readFile(fontPath(file))} {
    REQUIRE_MESSAGE(!bytes_.empty(), file);
    REQUIRE(FT_Init_FreeType(&library_) == 0);
    REQUIRE(FT_New_Memory_Face(library_, reinterpret_cast<const FT_Byte*>(bytes_.data()),
                               static_cast<FT_Long>(bytes_.size()), 0, &face_) == 0);
  }
  Face(const Face&) = delete;
  Face& operator=(const Face&) = delete;
  ~Face() {
    FT_Done_Face(face_);
    FT_Done_FreeType(library_);
  }

  [[nodiscard]] bool has(char32_t code) const { return FT_Get_Char_Index(face_, code) != 0; }
  [[nodiscard]] std::string family() const { return face_->family_name; }

  [[nodiscard]] std::optional<Glyph> pixelGlyph(char32_t code) const {
    if (!has(code) || FT_Set_Pixel_Sizes(face_, 0, face_->units_per_EM / UnitsPerPixel) != 0 ||
        FT_Load_Char(face_, code, FT_LOAD_RENDER | FT_LOAD_TARGET_MONO) != 0) {
      return std::nullopt;
    }
    const FT_Bitmap& bitmap = face_->glyph->bitmap;
    Glyph glyph{.left = face_->glyph->bitmap_left,
                .top = face_->glyph->bitmap_top,
                .width = static_cast<int>(bitmap.width),
                .rows = static_cast<int>(bitmap.rows),
                .pixels = {}};
    for (int y = 0; y < glyph.rows; ++y) {
      const unsigned char* row = bitmap.buffer + (static_cast<std::ptrdiff_t>(y) * bitmap.pitch);
      for (int x = 0; x < glyph.width; ++x) {
        glyph.pixels.push_back((row[x / 8] & (0x80 >> (x % 8))) != 0);
      }
    }
    return glyph;
  }

private:
  std::string bytes_;
  FT_Library library_ = nullptr;
  FT_Face face_ = nullptr;
};

bool hasCrossbar(const Glyph& glyph) {
  for (int y = 0; y < glyph.rows; ++y) {
    bool full = glyph.width > 0;
    for (int x = 0; x < glyph.width && full; ++x) {
      full = glyph.at(x, y);
    }
    if (full) {
      return true;
    }
  }
  return false;
}

int difference(const Glyph& one, const Glyph& other) {
  const int left = std::min(one.left, other.left);
  const int top = std::max(one.top, other.top);
  const int right = std::max(one.left + one.width, other.left + other.width);
  const int bottom = std::min(one.top - one.rows, other.top - other.rows);
  const auto inkAt = [](const Glyph& glyph, int x, int y) {
    const int column = x - glyph.left;
    const int row = glyph.top - y;
    return column >= 0 && row >= 0 && column < glyph.width && row < glyph.rows &&
           glyph.at(column, row);
  };
  int count = 0;
  for (int y = top; y > bottom; --y) {
    for (int x = left; x < right; ++x) {
      count += inkAt(one, x, y) != inkAt(other, x, y) ? 1 : 0;
    }
  }
  return count;
}

bool climbsBetweenStems(const Glyph& glyph) {
  const int feet = glyph.rows - 1;
  int gapLeft = 0;
  while (gapLeft < glyph.width && glyph.at(gapLeft, feet)) {
    ++gapLeft;
  }
  int gapRight = glyph.width - 1;
  while (gapRight >= 0 && glyph.at(gapRight, feet)) {
    --gapRight;
  }
  if (gapRight - gapLeft < 2) {
    return false;
  }
  int previousRow = glyph.rows;
  for (int x = gapLeft; x <= gapRight; ++x) {
    int inkRow = -1;
    for (int y = 0; y < glyph.rows && inkRow < 0; ++y) {
      if (glyph.at(0, y) && glyph.at(glyph.width - 1, y) && glyph.at(x, y)) {
        inkRow = y;
      }
    }
    if (inkRow < 0 || inkRow >= previousRow) {
      return false;
    }
    previousRow = inkRow;
  }
  return true;
}

std::string originalLookFallback() {
  const std::string yaml =
      readFile(std::filesystem::path{DWE_GAME_PACK} / "ui/fonts/original_look.yaml");
  const std::string_view key = "fallback_family: \"";
  const std::size_t start = yaml.find(key);
  if (start == std::string::npos) {
    return {};
  }
  const std::size_t from = start + key.size();
  return yaml.substr(from, yaml.find('"', from) - from);
}

} // namespace

TEST_CASE("every UI font has each Cyrillic letter") {
  for (const std::string_view file :
       {"noto_sans_regular.ttf", "noto_sans_bold.ttf", "pixel_body.ttf", "pixel_small.ttf"}) {
    const Face face{file};
    for (const char32_t letter : cyrillicLetters()) {
      CHECK_MESSAGE(face.has(letter), std::format("{} misses {}", file, utf8(letter)));
    }
  }
}

TEST_CASE("every UI font draws each Cyrillic letter apart") {
  for (const std::string_view file :
       {"noto_sans_regular.ttf", "noto_sans_bold.ttf", "pixel_body.ttf", "pixel_small.ttf"}) {
    const Face face{file};
    const std::vector<char32_t> letters = cyrillicLetters();
    std::vector<Glyph> glyphs;
    for (const char32_t letter : letters) {
      const auto glyph = face.pixelGlyph(letter);
      REQUIRE_MESSAGE(glyph.has_value(), std::format("{} {}", file, utf8(letter)));
      glyphs.push_back(*glyph);
    }
    for (std::size_t one = 0; one < glyphs.size(); ++one) {
      for (std::size_t other = one + 1; other < glyphs.size(); ++other) {
        CHECK_MESSAGE(glyphs[one] != glyphs[other],
                      std::format("{}: {} and {} are drawn the same", file, utf8(letters[one]),
                                  utf8(letters[other])));
      }
    }
  }
}

TEST_CASE("the original look draws Cyrillic with a checked pixel face") {
  CHECK(originalLookFallback() == Face{"pixel_small.ttf"}.family());
}

TEST_CASE("the pixel fonts keep look-alike Cyrillic letters readable apart") {
  for (const std::string_view file : {"pixel_body.ttf", "pixel_small.ttf"}) {
    const Face face{file};
    const std::vector<char32_t> letters = cyrillicLetters();
    std::vector<Glyph> glyphs;
    for (const char32_t letter : letters) {
      const auto glyph = face.pixelGlyph(letter);
      REQUIRE_MESSAGE(glyph.has_value(), std::format("{} {}", file, utf8(letter)));
      glyphs.push_back(*glyph);
    }
    for (std::size_t one = 0; one < glyphs.size(); ++one) {
      for (std::size_t other = one + 1; other < glyphs.size(); ++other) {
        CHECK_MESSAGE(difference(glyphs[one], glyphs[other]) >= 2,
                      std::format("{}: {} and {} differ by one pixel", file, utf8(letters[one]),
                                  utf8(letters[other])));
      }
    }
    const auto em = face.pixelGlyph(U'м');
    const auto en = face.pixelGlyph(U'н');
    REQUIRE(em.has_value());
    REQUIRE(en.has_value());
    CHECK_MESSAGE(hasCrossbar(*en), file);
    CHECK_MESSAGE(!hasCrossbar(*em), std::format("{}: the lowercase em reads as en", file));
    for (const char32_t letter : {U'И', U'и', U'Й', U'й'}) {
      const auto glyph = face.pixelGlyph(letter);
      REQUIRE(glyph.has_value());
      CHECK_MESSAGE(!hasCrossbar(*glyph), std::format("{}: {} reads as en", file, utf8(letter)));
      CHECK_MESSAGE(climbsBetweenStems(*glyph),
                    std::format("{}: {} has no three-column diagonal", file, utf8(letter)));
    }
  }
}

} // namespace dwe::ui
