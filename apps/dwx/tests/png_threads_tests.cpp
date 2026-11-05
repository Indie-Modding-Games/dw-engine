// dwx encodes the PNGs of a disc on several threads and checkContent decodes them back on several
// threads: either side sharing state between threads would give bytes that change from run to run.

#include <dwe/assets/image.hpp>
#include <dwe/formats/image/png.hpp>
#include <dwe/formats/image/tim.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t ImageCount = 96;
constexpr unsigned ThreadCount = 16;
constexpr int Rounds = 3;

// An image of its own for each index, with runs and noise so deflate builds dynamic Huffman trees.
dwe::formats::image::RgbaImage imageOf(std::size_t index) {
  const auto width = static_cast<std::uint32_t>(16 + (index % 7) * 24);
  const auto height = static_cast<std::uint32_t>(16 + (index % 5) * 20);
  dwe::formats::image::RgbaImage image{.width = width, .height = height, .pixels = {}};
  std::uint32_t seed = static_cast<std::uint32_t>(index) * 2654435761U + 1U;
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      seed = seed * 1664525U + 1013904223U;
      const bool noisy = ((x / 8) + (y / 8) + index) % 3 == 0;
      const auto value = static_cast<std::uint8_t>(noisy ? seed >> 24 : (x * 4 + y) & 0xFFU);
      image.pixels.push_back({.r = value,
                              .g = static_cast<std::uint8_t>(value ^ (y & 0xFFU)),
                              .b = static_cast<std::uint8_t>(index * 37),
                              .a = static_cast<std::uint8_t>(noisy ? 0xFF : 0x80)});
    }
  }
  return image;
}

std::vector<std::byte> encoded(const dwe::formats::image::RgbaImage& image) {
  auto png = dwe::formats::image::encodePngRgba(image);
  REQUIRE_MESSAGE(png.has_value(), png.error().message);
  return *png;
}

// Runs work(i) for every image on ThreadCount threads at once.
template <typename Work>
void onThreads(Work work) {
  std::atomic<std::size_t> next{0};
  const auto worker = [&] {
    for (std::size_t i = next++; i < ImageCount; i = next++) {
      work(i);
    }
  };
  std::vector<std::thread> threads;
  for (unsigned i = 1; i < ThreadCount; ++i) {
    threads.emplace_back(worker);
  }
  worker();
  for (auto& thread : threads) {
    thread.join();
  }
}

} // namespace

TEST_CASE("PNGs encoded and decoded on many threads at once match a run on one thread") {
  std::vector<dwe::formats::image::RgbaImage> images;
  std::vector<std::vector<std::byte>> serial;
  for (std::size_t i = 0; i < ImageCount; ++i) {
    images.push_back(imageOf(i));
    serial.push_back(encoded(images.back()));
  }
  for (int round = 0; round < Rounds; ++round) {
    std::vector<std::vector<std::byte>> parallel(ImageCount);
    onThreads([&](std::size_t i) {
      parallel[i] =
          dwe::formats::image::encodePngRgba(images[i]).value_or(std::vector<std::byte>{});
    });
    std::vector<std::optional<std::string>> wrong(ImageCount);
    onThreads([&](std::size_t i) {
      const auto decoded = dwe::assets::decodePng(serial[i]);
      if (!decoded) {
        wrong[i] = decoded.error().message;
        return;
      }
      const auto& pixels = images[i].pixels;
      const bool same =
          decoded->width == images[i].width && decoded->height == images[i].height &&
          decoded->pixels.size() == pixels.size() * 4 &&
          std::ranges::equal(pixels, std::views::iota(std::size_t{0}, pixels.size()),
                             [&](const auto& pixel, std::size_t at) {
                               const auto* rgba = &decoded->pixels[at * 4];
                               return std::to_integer<std::uint8_t>(rgba[0]) == pixel.r &&
                                      std::to_integer<std::uint8_t>(rgba[1]) == pixel.g &&
                                      std::to_integer<std::uint8_t>(rgba[2]) == pixel.b &&
                                      std::to_integer<std::uint8_t>(rgba[3]) == pixel.a;
                             });
      if (!same) {
        wrong[i] = "decodes to other pixels";
      }
    });
    for (std::size_t i = 0; i < ImageCount; ++i) {
      CAPTURE(round);
      CAPTURE(i);
      CHECK(parallel[i] == serial[i]);
      CHECK_MESSAGE(!wrong[i], wrong[i].value_or(""));
    }
  }
}
