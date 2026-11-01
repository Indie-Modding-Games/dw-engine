#include "flame_texture.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace dwe::app {

namespace {

struct FlameSpecies {
  std::string_view species;
  std::uint32_t width;
};

// animateEntityTexture's species (types 0x09, 0x85, 0x68; 0x15, 0x6a, 0x8f; 0x65, 0x45; 0x7f) and
// their strips' widths in texels.
constexpr std::array<FlameSpecies, 9> Flames{{
    {.species = "mera", .width = 0x1e * 4},
    {.species = "emer", .width = 0x1e * 4},
    {.species = "blmr", .width = 0x1e * 4},
    {.species = "bird", .width = 0x28 * 4},
    {.species = "sabd", .width = 0x28 * 4},
    {.species = "ebir", .width = 0x28 * 4},
    {.species = "dkrz", .width = 0x10 * 4},
    {.species = "flar", .width = 0x10 * 4},
    {.species = "puti", .width = 0x2c * 4},
}};

constexpr std::size_t BytesPerTexel = 4;

} // namespace

std::optional<std::uint32_t> flameStripWidth(std::string_view model) {
  const std::size_t first = model.find('/');
  if (first == std::string_view::npos) {
    return std::nullopt;
  }
  const std::string_view rest = model.substr(first + 1);
  const std::size_t second = rest.find('/');
  if (second == std::string_view::npos || rest.substr(second + 1) != "model") {
    return std::nullopt;
  }
  const auto found = std::ranges::find(Flames, rest.substr(0, second), &FlameSpecies::species);
  return found != Flames.end() ? std::optional{found->width} : std::nullopt;
}

std::optional<std::uint32_t> flameStripFrame(std::uint64_t frame) {
  if (frame % 2 != 0) {
    return std::nullopt;
  }
  if (frame % 6 == 0) {
    return 2U;
  }
  return frame % 4 == 0 ? 1U : 0U;
}

std::vector<std::byte> flameStripPixels(const assets::Image& image, std::uint32_t width,
                                        std::uint32_t strip) {
  const std::size_t top = static_cast<std::size_t>(strip + 1) * FlameStripRows;
  if (width > image.width || top + FlameStripRows > image.height ||
      image.pixels.size() < std::size_t{image.width} * image.height * BytesPerTexel) {
    return {};
  }
  std::vector<std::byte> pixels;
  pixels.reserve(std::size_t{width} * FlameStripRows * BytesPerTexel);
  for (std::size_t row = top; row < top + FlameStripRows; ++row) {
    const auto first =
        image.pixels.begin() + static_cast<std::ptrdiff_t>(row * image.width * BytesPerTexel);
    pixels.insert(pixels.end(), first,
                  first + static_cast<std::ptrdiff_t>(std::size_t{width} * BytesPerTexel));
  }
  return pixels;
}

} // namespace dwe::app
