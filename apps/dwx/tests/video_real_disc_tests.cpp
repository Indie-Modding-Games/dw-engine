// Runs only when DWE_DISC names the user's disc image (scripts/dev.sh mounts it); the CI never has
// one. Every movie is extracted, which checks every frame's bitstream and decodes all the audio;
// the shortest one is also decoded to the last frame, into a sink that only counts the bytes
// (decoding all four takes minutes in a sanitizer build).

#include <dwe/core/yaml.hpp>
#include <dwe/formats/disc/disc_image.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "disc_description.hpp"
#include "extractor.hpp"
#include "video_extractors.hpp"

using namespace dwe::dwx;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

std::string textOf(std::span<const std::byte> bytes) {
  std::string text;
  for (const std::byte value : bytes) {
    text += static_cast<char>(value);
  }
  return text;
}

} // namespace

TEST_CASE("the user's disc: every movie is extracted without unknowns") {
  const std::optional<std::filesystem::path> mountedPath = dwe::test_support::userDisc();
  if (!mountedPath) {
    MESSAGE("DWE_DISC is not set: skipping the test on the user's disc");
    return;
  }
  const std::filesystem::path& path = *mountedPath;
  auto image = dwe::formats::disc::DiscImage::open(path);
  REQUIRE_MESSAGE(image.has_value(), errorOf(image));
  const auto volume = dwe::formats::disc::readIsoVolume(*image);
  REQUIRE_MESSAGE(volume.has_value(), errorOf(volume));
  const DiscDescription description = describeDisc(*image, *volume);
  const Disc disc{*image, *volume, description.info,
                  description.tables ? &*description.tables : nullptr};

  std::size_t movies = 0;
  for (const auto& file : volume->files) {
    if (!handlesMovie(file.path)) {
      continue;
    }
    ++movies;
    const auto extraction = extractMovie(disc, file);
    REQUIRE_MESSAGE(extraction.has_value(), file.path, ": ", errorOf(extraction));
    CHECK_MESSAGE(extraction->unknowns.empty(), file.path, ": ",
                  extraction->unknowns.empty() ? std::string{} : extraction->unknowns.front());
    REQUIRE(extraction->outputs.size() == 3);

    const auto& document = extraction->outputs.back();
    const auto yaml = dwe::core::parseYaml(textOf(document.bytes));
    REQUIRE(yaml.has_value());
    const auto* video = yaml->find("video");
    CHECK(video->find("width")->text() == "320");
    CHECK(video->find("height")->text() == "176");
    CHECK(video->find("fps")->find("numerator")->text() == "30");
    CHECK(video->find("fps")->find("denominator")->text() == "1");
    CHECK(yaml->find("audio")->find("sample_rate")->text() == "37800");
    if (!description.info.version.empty()) {
      CHECK(yaml->find("playback")->find("frame_count") != nullptr);
    }
    const auto frames = std::stoull(video->find("frames")->text());
    MESSAGE(file.path, ": ", frames, " frames, ", extraction->outputs[1].bytes.size(),
            " bytes of audio");
    if (file.path != "MOVIE/ED2.STR") {
      continue;
    }

    std::size_t bytes = 0;
    std::size_t parts = 0;
    const auto streamed = extraction->outputs.front().stream([&](std::span<const std::byte> part) {
      bytes += part.size();
      ++parts;
      return dwe::core::Result<void>{};
    });
    REQUIRE_MESSAGE(streamed.has_value(), file.path, ": ", errorOf(streamed));
    CHECK(parts == frames + 1);
    const std::size_t frameSize = 6 + (std::size_t{320} * 176 * 3 / 2);
    CHECK(bytes - (frames * frameSize) ==
          std::string_view{"YUV4MPEG2 W320 H176 F30:1 Ip A1:1 C420jpeg "
                           "XCOLORRANGE=FULL\n"}
              .size());
  }
  CHECK(movies == 4);
}
