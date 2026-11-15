#include <dwe/render/scenery_upscale.hpp>
#include <dwe/render/texture_upscale_job.hpp>

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <utility>
#include <vector>

using dwe::assets::Image;
using dwe::render::modelTexturePixels;
using dwe::render::TextureUpscaleJob;
using namespace std::chrono_literals;

namespace {

Image image(std::uint32_t width, std::uint32_t height, std::byte value) {
  return {.width = width,
          .height = height,
          .pixels = std::vector<std::byte>(std::size_t{width} * height * 4, value)};
}

} // namespace

TEST_CASE("a texture upscale job returns at once and hands its pixels over once it finishes") {
  // The upscale is held until the test lets it go: the model keeps drawing its plain textures.
  std::promise<void> gate;
  const std::shared_future<void> open = gate.get_future().share();
  TextureUpscaleJob job{{image(2, 2, std::byte{0xFF}), image(1, 1, std::byte{0x40})},
                        [open](const Image& source) {
                          open.wait();
                          return modelTexturePixels(source, 1, nullptr);
                        }};
  CHECK_FALSE(job.finished());
  CHECK_FALSE(job.take().has_value());
  gate.set_value();
  REQUIRE(job.wait(10s));
  CHECK(job.finished());
  auto pixels = job.take();
  REQUIRE(pixels.has_value());
  REQUIRE(pixels->size() == 2);
  // Doubled: four times the texels of each image.
  CHECK((*pixels)[0].size() == std::size_t{4} * 4 * 4);
  CHECK((*pixels)[1].size() == std::size_t{2} * 2 * 4);
  CHECK((*pixels)[0] == dwe::render::upscaleRgba(2, 2, image(2, 2, std::byte{0xFF}).pixels, 1));
  // Taken once.
  CHECK_FALSE(job.take().has_value());
}

TEST_CASE("a cancelled texture upscale job stops after the image under way and hands nothing") {
  std::promise<void> gate;
  const std::shared_future<void> open = gate.get_future().share();
  std::atomic<int> made{0};
  TextureUpscaleJob job{{image(2, 2, std::byte{1}), image(2, 2, std::byte{2})},
                        [open, &made](const Image& source) {
                          open.wait();
                          ++made;
                          return modelTexturePixels(source, 1, nullptr);
                        }};
  job.cancel();
  gate.set_value();
  REQUIRE(job.wait(10s));
  CHECK(made.load() <= 1);
  CHECK_FALSE(job.take().has_value());
}

TEST_CASE("a model texture is doubled when upscaled and keeps its size when smooth") {
  const Image source = image(3, 2, std::byte{0x80});
  CHECK(modelTexturePixels(source, 1, nullptr).size() == source.pixels.size() * 4);
  CHECK(modelTexturePixels(source, 0, nullptr).size() == source.pixels.size());
}
