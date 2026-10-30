#include "screenshot.hpp"

#include <dwe/assets/image.hpp>

#include <format>
#include <fstream>
#include <ios>
#include <utility>

namespace dwe::app {

core::Result<void> writeScreenshot(rhi::CapturedImage captured, const std::filesystem::path& path) {
  const assets::Image image{
      .width = captured.width, .height = captured.height, .pixels = std::move(captured.pixels)};
  const auto png = assets::encodePng(image);
  if (!png) {
    return std::unexpected{png.error()};
  }
  std::ofstream file{path, std::ios::binary};
  if (!file.write(reinterpret_cast<const char*>(png->data()),
                  static_cast<std::streamsize>(png->size()))) {
    return core::fail(std::format("cannot write {}", path.string()));
  }
  return {};
}

} // namespace dwe::app
