#include <dwe/core/sha1.hpp>
#include <dwe/formats/disc/iso9660.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "disc_fixtures.hpp"

using namespace dwe::formats::disc;

namespace {

using Bytes = std::vector<std::byte>;

fixtures::IsoBuilder sampleBuilder() {
  fixtures::IsoBuilder builder;
  builder.addFile("SYSTEM.CNF", fixtures::patternBytes(40, 1));
  builder.addFile("README.", fixtures::patternBytes(10, 2));
  builder.addFile("EMPTY.DAT", {});
  builder.addFile("DATA/A.BIN", fixtures::patternBytes(3000, 3));
  builder.addFile("DATA/SUB/B.BIN", fixtures::patternBytes(100, 4));
  return builder;
}

// Returns the offset of the record named name in a directory sector of a cooked image.
std::size_t recordOffset(const Bytes& image, std::uint32_t directoryLba, std::string_view name) {
  const std::size_t base = std::size_t{directoryLba} * Form1DataSize;
  std::size_t offset = 0;
  while (offset < Form1DataSize) {
    const auto length = std::to_integer<std::size_t>(image[base + offset]);
    REQUIRE(length != 0);
    const auto nameLength = std::to_integer<std::size_t>(image[base + offset + 32]);
    const std::string recordName(reinterpret_cast<const char*>(&image[base + offset + 33]),
                                 nameLength);
    if (recordName == name) {
      return base + offset;
    }
    offset += length;
  }
  FAIL("record not found");
  return 0;
}

dwe::core::Result<IsoVolume> readCooked(const Bytes& image) {
  MemorySectorSource source{image, SectorLayout::Cooked};
  return readIsoVolume(source);
}

} // namespace

TEST_CASE("every file of the volume is listed with its path, sector and size") {
  auto builder = sampleBuilder();
  const auto image = builder.build();
  const auto volume = readCooked(image);
  REQUIRE(volume.has_value());
  CHECK(volume->volumeId == "FIXTURE");
  CHECK(volume->volumeSectors == image.size() / Form1DataSize);
  REQUIRE(volume->files.size() == 5);
  CHECK(volume->files[0] ==
        IsoFile{.path = "DATA/A.BIN", .lba = builder.lbaOf("DATA/A.BIN"), .size = 3000});
  CHECK(volume->files[1].path == "DATA/SUB/B.BIN");
  CHECK(volume->files[2] ==
        IsoFile{.path = "EMPTY.DAT", .lba = builder.lbaOf("EMPTY.DAT"), .size = 0});
  CHECK(volume->files[3].path == "README");
  CHECK(volume->files[4].path == "SYSTEM.CNF");
}

TEST_CASE("a raw image gives the same volume and the same file contents") {
  auto builder = sampleBuilder();
  const auto raw = fixtures::toRawImage(builder.build());
  MemorySectorSource source{raw, SectorLayout::Raw};
  const auto volume = readIsoVolume(source);
  REQUIRE(volume.has_value());
  const auto file = std::ranges::find(volume->files, std::string{"DATA/A.BIN"}, &IsoFile::path);
  REQUIRE(file != volume->files.end());
  CHECK(sectorSpan(*file) == 2);

  const auto contents = readIsoFile(source, *file);
  REQUIRE(contents.has_value());
  CHECK(*contents == fixtures::patternBytes(3000, 3));
  const auto digest = hashIsoFile(source, *file);
  REQUIRE(digest.has_value());
  CHECK(*digest == dwe::core::sha1(*contents));
}

TEST_CASE("form 2 sectors are hashed whole and cannot be read as form 1") {
  auto builder = sampleBuilder();
  auto raw = fixtures::toRawImage(builder.build());
  const IsoFile file{.path = "DATA/A.BIN", .lba = builder.lbaOf("DATA/A.BIN"), .size = 4096};
  const auto form2 =
      fixtures::makeRawSector(fixtures::patternBytes(Form2DataSize, 9), 2, SectorForm::Form2);
  std::ranges::copy(form2,
                    raw.begin() + static_cast<std::ptrdiff_t>((file.lba + 1) * RawSectorSize));
  MemorySectorSource source{raw, SectorLayout::Raw};

  CHECK_FALSE(readIsoFile(source, file).has_value());
  auto expected = fixtures::patternBytes(3000, 3);
  expected.resize(Form1DataSize);
  const auto tail = fixtures::patternBytes(Form2DataSize, 9);
  expected.insert(expected.end(), tail.begin(), tail.end());
  const auto digest = hashIsoFile(source, file);
  REQUIRE(digest.has_value());
  CHECK(*digest == dwe::core::sha1(expected));
}

TEST_CASE("truncated images and bad descriptors are rejected") {
  auto builder = sampleBuilder();
  const auto image = builder.build();
  CHECK_FALSE(readCooked(Bytes(image.begin(), image.begin() + (16 * Form1DataSize))).has_value());
  CHECK_FALSE(readCooked(Bytes(image.begin(), image.begin() + (18 * Form1DataSize))).has_value());
  CHECK_FALSE(readCooked(Bytes(image.begin(), image.end() - Form1DataSize)).has_value());

  auto badMagic = image;
  badMagic[(16 * Form1DataSize) + 1] = std::byte{'X'};
  CHECK_FALSE(readCooked(badMagic).has_value());

  auto badBlockSize = image;
  badBlockSize[(16 * Form1DataSize) + 129] = std::byte{0x04};
  CHECK_FALSE(readCooked(badBlockSize).has_value());
}

TEST_CASE("bad records, names and offsets are rejected") {
  auto builder = sampleBuilder();
  const auto image = builder.build();
  const auto data = builder.lbaOf("DATA");

  auto outside = image;
  outside[recordOffset(image, data, "A.BIN;1") + 4] = std::byte{0x7F};
  CHECK_FALSE(readCooked(outside).has_value());

  auto shortRecord = image;
  shortRecord[recordOffset(image, data, "A.BIN;1")] = std::byte{20};
  CHECK_FALSE(readCooked(shortRecord).has_value());

  auto longName = image;
  longName[recordOffset(image, data, "A.BIN;1") + 32] = std::byte{200};
  CHECK_FALSE(readCooked(longName).has_value());

  auto slash = image;
  slash[recordOffset(image, data, "A.BIN;1") + 34] = std::byte{'/'};
  CHECK_FALSE(readCooked(slash).has_value());

  auto dotDot = image;
  const std::size_t sub = recordOffset(image, data, "SUB");
  dotDot[sub + 32] = std::byte{2};
  dotDot[sub + 33] = std::byte{'.'};
  dotDot[sub + 34] = std::byte{'.'};
  CHECK_FALSE(readCooked(dotDot).has_value());
}

TEST_CASE("directory loops and runaway nesting are rejected") {
  auto builder = sampleBuilder();
  const auto image = builder.build();

  auto loop = image;
  const std::size_t sub = recordOffset(image, builder.lbaOf("DATA"), "SUB");
  loop[sub + 2] = static_cast<std::byte>(fixtures::IsoBuilder::RootLba);
  const auto looped = readCooked(loop);
  REQUIRE_FALSE(looped.has_value());
  CHECK(looped.error().message.find("loop") != std::string::npos);

  fixtures::IsoBuilder deep;
  std::string path;
  for (int i = 0; i < 40; ++i) {
    path += "D/";
  }
  deep.addFile(path + "F.BIN", fixtures::patternBytes(1, 0));
  CHECK_FALSE(readCooked(deep.build()).has_value());
}
