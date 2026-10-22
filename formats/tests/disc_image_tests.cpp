#include <dwe/formats/disc/disc_image.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>

#include "disc_fixtures.hpp"

using namespace dwe::formats::disc;

namespace {

// A folder of its own under the system's temporary folder, removed when the test ends.
class TemporaryFolder {
public:
  explicit TemporaryFolder(std::string_view name)
      : path_(std::filesystem::temp_directory_path() / std::filesystem::path{name}) {
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  TemporaryFolder(const TemporaryFolder&) = delete;
  TemporaryFolder& operator=(const TemporaryFolder&) = delete;
  TemporaryFolder(TemporaryFolder&&) = delete;
  TemporaryFolder& operator=(TemporaryFolder&&) = delete;
  ~TemporaryFolder() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
  std::filesystem::path path_;
};

void writeFile(const std::filesystem::path& path, std::span<const std::byte> bytes) {
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
}

void writeText(const std::filesystem::path& path, std::string_view text) {
  writeFile(path, std::as_bytes(std::span{text}));
}

std::vector<std::byte> cookedImage(std::size_t sectors) {
  std::vector<std::byte> image(sectors * Form1DataSize);
  for (std::size_t i = 0; i < sectors; ++i) {
    image[i * Form1DataSize] = static_cast<std::byte>(i);
  }
  return image;
}

} // namespace

TEST_CASE("a CUE sheet opens the BIN next to it") {
  const TemporaryFolder folder{"dwe-disc-image-cue"};
  writeFile(folder.path() / "Game (Region).bin", fixtures::toRawImage(cookedImage(3)));
  writeText(folder.path() / "Game (Region).cue",
            "FILE \"Game (Region).bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n");

  auto image = DiscImage::open(folder.path() / "Game (Region).cue");
  REQUIRE(image.has_value());
  CHECK(image->layout() == SectorLayout::Raw);
  CHECK(image->dataFile().filename() == "Game (Region).bin");
  CHECK(image->sectorCount() == 3);
  const auto sector = image->readSector(2);
  REQUIRE(sector.has_value());
  CHECK(sector->userData[0] == std::byte{2});
  CHECK_FALSE(image->readSector(3).has_value());
}

TEST_CASE("a BIN or an ISO opens without a sheet") {
  const TemporaryFolder folder{"dwe-disc-image-plain"};
  writeFile(folder.path() / "game.bin", fixtures::toRawImage(cookedImage(2)));
  writeFile(folder.path() / "game.iso", cookedImage(2));

  const auto raw = DiscImage::open(folder.path() / "game.bin");
  REQUIRE(raw.has_value());
  CHECK(raw->layout() == SectorLayout::Raw);

  auto cooked = DiscImage::open(folder.path() / "game.iso");
  REQUIRE(cooked.has_value());
  CHECK(cooked->layout() == SectorLayout::Cooked);
  const auto sector = cooked->readSector(1);
  REQUIRE(sector.has_value());
  CHECK(sector->userData[0] == std::byte{1});
}

TEST_CASE("broken images and sheets are reported") {
  const TemporaryFolder folder{"dwe-disc-image-broken"};
  CHECK_FALSE(DiscImage::open(folder.path() / "missing.bin").has_value());

  auto truncated = fixtures::toRawImage(cookedImage(2));
  truncated.resize(truncated.size() - 1);
  writeFile(folder.path() / "truncated.bin", truncated);
  CHECK_FALSE(DiscImage::open(folder.path() / "truncated.bin").has_value());

  writeText(folder.path() / "missing.cue",
            "FILE \"nowhere.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n");
  CHECK_FALSE(DiscImage::open(folder.path() / "missing.cue").has_value());

  writeFile(folder.path() / "cooked.bin", cookedImage(2));
  writeText(folder.path() / "wrong.cue",
            "FILE \"cooked.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n");
  auto wrong = DiscImage::open(folder.path() / "wrong.cue");
  if (wrong) {
    CHECK_FALSE(wrong->readSector(0).has_value());
  }
}
