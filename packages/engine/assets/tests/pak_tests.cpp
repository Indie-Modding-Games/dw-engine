#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using dwe::assets::AssetId;
using dwe::assets::DiskFileSystem;
using dwe::assets::FoundPack;
using dwe::assets::PackLocation;
using dwe::assets::PakCompression;
using dwe::assets::PakEntrySize;
using dwe::assets::PakFileSystem;
using dwe::assets::PakHeaderSize;
using dwe::assets::VirtualFileSystem;
using dwe::assets::writePak;
using dwe::test_support::TempFolder;

namespace {

std::vector<std::byte> fileBytes(const std::filesystem::path& file) {
  std::ifstream stream{file, std::ios::binary};
  const std::vector<char> chars{std::istreambuf_iterator<char>{stream}, {}};
  std::vector<std::byte> bytes(chars.size());
  for (std::size_t i = 0; i < chars.size(); ++i) {
    bytes[i] = static_cast<std::byte>(chars[i]);
  }
  return bytes;
}

void writeBytes(const std::filesystem::path& file, const std::vector<std::byte>& bytes) {
  std::ofstream stream{file, std::ios::binary | std::ios::trunc};
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

std::uint64_t u64At(const std::vector<std::byte>& bytes, std::size_t offset) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < 8; ++i) {
    value |= std::to_integer<std::uint64_t>(bytes[offset + i]) << (8U * i);
  }
  return value;
}

std::uint32_t u32At(const std::vector<std::byte>& bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8U * i);
  }
  return value;
}

void putU32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) {
    bytes[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
  }
}

// A synthetic base pack with nested folders, an empty file and a file whose size is not a multiple
// of the alignment.
void writeFixturePack(const TempFolder& folder) {
  folder.write("base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  folder.write("base/map/start/background.png", "not really a png, 23 b");
  folder.write("base/map/start/collision.yaml", "cells: []\n");
  folder.write("base/map/start_b/background.png", "b");
  folder.write("base/music/empty.mid", "");
  folder.write("base/readme.txt", std::string(100, 'x'));
}

std::filesystem::path writeFixturePak(const TempFolder& folder) {
  writeFixturePack(folder);
  const std::filesystem::path archive = folder.path() / "base.pak";
  const auto entries = writePak(DiskFileSystem{}, folder.path() / "base", archive);
  REQUIRE(entries.has_value());
  return archive;
}

std::string openError(const std::filesystem::path& archive) {
  const auto pak = PakFileSystem::open(archive);
  REQUIRE_FALSE(pak.has_value());
  return pak.error().message;
}

} // namespace

TEST_CASE("a .pak mounts with the same assets and bytes as its folder") {
  const TempFolder folder;
  const std::filesystem::path archive = writeFixturePak(folder);
  const DiskFileSystem disk;
  const auto pak = PakFileSystem::open(archive);
  REQUIRE(pak.has_value());

  const std::array folderPack{PackLocation{.fileSystem = &disk, .root = folder.path() / "base"}};
  const std::array pakPack{PackLocation{.fileSystem = pak->get(), .root = archive}};
  const auto fromFolder = VirtualFileSystem::mount(folderPack);
  const auto fromPak = VirtualFileSystem::mount(pakPack);
  REQUIRE(fromFolder.has_value());
  REQUIRE(fromPak.has_value());

  CHECK(fromPak->packs().front().manifest.id == "base");
  REQUIRE(fromPak->ids() == fromFolder->ids());
  CHECK(fromPak->ids().size() == 5);
  for (const AssetId& id : fromFolder->ids()) {
    const auto expected = fromFolder->read(id);
    const auto actual = fromPak->read(id);
    REQUIRE(expected.has_value());
    REQUIRE(actual.has_value());
    CHECK_MESSAGE(*actual == *expected, id.str());
  }
  CHECK(fromPak->resolve(*AssetId::parse("map/start/collision"))->file ==
        archive / "map/start/collision.yaml");
  // The text compresses; the short files do not, so they are stored as they are.
  CHECK((*pak)->entries()[4].compression == PakCompression::Zstd);
  CHECK((*pak)->entries()[0].compression == PakCompression::None);
}

TEST_CASE("a folder pack overrides a .pak pack below it") {
  const TempFolder folder;
  const std::filesystem::path archive = writeFixturePak(folder);
  folder.write("hd/pack.yaml",
               "id: hd\nversion: 0.1.0\nlayer: remaster\ndependencies:\n  - id: base\n"
               "    min_version: 1.0.0\n");
  folder.write("hd/map/start/background.png", "hd");
  const DiskFileSystem disk;
  const auto pak = PakFileSystem::open(archive);
  REQUIRE(pak.has_value());

  const std::array packs{PackLocation{.fileSystem = &disk, .root = folder.path() / "hd"},
                         PackLocation{.fileSystem = pak->get(), .root = archive}};
  const auto vfs = VirtualFileSystem::mount(packs);
  REQUIRE(vfs.has_value());
  const auto background = vfs->read(*AssetId::parse("map/start/background"));
  const auto collision = vfs->read(*AssetId::parse("map/start/collision"));
  REQUIRE(background.has_value());
  REQUIRE(collision.has_value());
  CHECK(*background == std::vector{std::byte{'h'}, std::byte{'d'}});
  CHECK(collision->size() == 10);
  CHECK(vfs->sources(*AssetId::parse("map/start/background")).size() == 2);
}

TEST_CASE("the .pak archives of a content folder are found, opened and mounted with its folders") {
  const TempFolder folder;
  writeFixturePack(folder);
  folder.write("content/hd/pack.yaml",
               "id: hd\nversion: 0.1.0\nlayer: remaster\ndependencies:\n  - id: base\n"
               "    min_version: 1.0.0\n");
  folder.write("content/hd/map/start/background.png", "hd");
  const DiskFileSystem disk;
  REQUIRE(writePak(disk, folder.path() / "base", folder.path() / "content/base.pak").has_value());
  // Built from hd/, so the folder next to it is used instead.
  REQUIRE(
      writePak(disk, folder.path() / "content/hd", folder.path() / "content/hd.pak").has_value());

  const auto found = dwe::assets::findPacks(disk, folder.path() / "content");
  REQUIRE_MESSAGE(found.has_value(), found.error().message);
  CHECK(found->packs ==
        std::vector{
            FoundPack{.path = folder.path() / "content/base.pak", .kind = FoundPack::Kind::Archive},
            FoundPack{.path = folder.path() / "content/hd", .kind = FoundPack::Kind::Folder}});
  CHECK(found->hidden == std::vector{folder.path() / "content/hd.pak"});

  const auto opened = dwe::assets::openPacks(disk, found->packs, dwe::assets::PackOrigin::Engine);
  REQUIRE(opened.has_value());
  CHECK(opened->archives.size() == 1);
  REQUIRE(opened->locations.size() == 2);
  CHECK(opened->locations[0].fileSystem == opened->archives[0].get());
  CHECK(opened->locations[1].fileSystem == &disk);
  const auto vfs = VirtualFileSystem::mount(opened->locations);
  REQUIRE(vfs.has_value());
  const auto background = vfs->read(*AssetId::parse("map/start/background"));
  REQUIRE(background.has_value());
  CHECK(*background == std::vector{std::byte{'h'}, std::byte{'d'}});
  CHECK(vfs->resolve(*AssetId::parse("map/start/collision"))->file ==
        folder.path() / "content/base.pak/map/start/collision.yaml");
}

TEST_CASE("opening packs fails on an archive that does not open") {
  const TempFolder folder;
  folder.write("content/broken.pak", "not an archive");
  const DiskFileSystem disk;
  const auto found = dwe::assets::findPacks(disk, folder.path() / "content");
  REQUIRE(found.has_value());
  REQUIRE(found->packs.size() == 1);
  const auto opened = dwe::assets::openPacks(disk, found->packs, dwe::assets::PackOrigin::Engine);
  REQUIRE_FALSE(opened.has_value());
  CHECK(opened.error().message.contains("broken.pak"));
}

TEST_CASE("a .pak is laid out as documented and written deterministically") {
  const TempFolder folder;
  const std::filesystem::path archive = writeFixturePak(folder);
  const auto again =
      writePak(DiskFileSystem{}, folder.path() / "base", folder.path() / "again.pak");
  REQUIRE(again.has_value());
  const auto bytes = fileBytes(archive);
  CHECK(bytes == fileBytes(folder.path() / "again.pak"));

  REQUIRE(again->size() == 5);
  // Sorted by asset id; each file starts at a multiple of 16 right after the previous one.
  CHECK((*again)[0].path == "map/start/background.png");
  CHECK((*again)[0].offset == PakHeaderSize);
  CHECK((*again)[1].path == "map/start/collision.yaml");
  CHECK((*again)[1].offset == 96);
  CHECK((*again)[2].path == "map/start_b/background.png");
  CHECK((*again)[3].path == "music/empty.mid");
  CHECK((*again)[3].size == 0);
  CHECK((*again)[4].path == "readme.txt");
  for (const auto& entry : *again) {
    CHECK(entry.offset % dwe::assets::PakAlignment == 0);
    if (entry.path != "readme.txt") {
      CHECK(entry.compression == PakCompression::None);
      CHECK(entry.storedSize == entry.size);
    }
  }
  CHECK((*again)[4].compression == PakCompression::Zstd);
  CHECK((*again)[4].size == 100);
  CHECK((*again)[4].storedSize < 100);
  CHECK(u32At(bytes, 0x08) == 2);
  const std::uint64_t tocOffset = u64At(bytes, 0x18);
  const std::uint64_t readme = tocOffset + (4 * PakEntrySize);
  CHECK(u64At(bytes, readme + 0x00) == (*again)[4].offset);
  CHECK(u64At(bytes, readme + 0x08) == (*again)[4].storedSize);
  CHECK(u64At(bytes, readme + 0x10) == 100);
  CHECK(u32At(bytes, readme + 0x20) == 1);
  // A zstd frame, starting with its magic.
  CHECK(u32At(bytes, (*again)[4].offset) == 0xFD2FB528);
  CHECK(tocOffset % 16 == 0);
  CHECK(u64At(bytes, 0x20) == tocOffset + (5 * PakEntrySize));
  const std::string manifest = "id: base\nversion: 1.0.0\nlayer: base\n";
  CHECK(u64At(bytes, 0x38) == manifest.size());
  CHECK(u64At(bytes, 0x30) + manifest.size() == bytes.size());
}

TEST_CASE("writing a .pak checks the pack") {
  const TempFolder folder;
  folder.write("bad/pack.yaml", "id: bad\nversion: 1.0.0\nlayer: base\n");
  folder.write("bad/Map/a.png", "a");
  const auto badName = writePak(DiskFileSystem{}, folder.path() / "bad", folder.path() / "bad.pak");
  REQUIRE_FALSE(badName.has_value());
  CHECK(badName.error().message ==
        "pack 'bad': file 'Map/a.png': asset id 'Map/a' may only use a-z, 0-9, '_' and '/'");

  folder.write("nomanifest/a.png", "a");
  CHECK_FALSE(writePak(DiskFileSystem{}, folder.path() / "nomanifest", folder.path() / "x.pak")
                  .has_value());
}

TEST_CASE("a damaged .pak is refused") {
  const TempFolder folder;
  const std::filesystem::path archive = writeFixturePak(folder);
  const auto good = fileBytes(archive);
  const std::filesystem::path damaged = folder.path() / "damaged.pak";
  const auto damage = [&](auto change) {
    auto bytes = good;
    change(bytes);
    writeBytes(damaged, bytes);
    return openError(damaged);
  };
  const std::uint64_t tocOffset = u64At(good, 0x18);

  CHECK(openError(folder.path() / "missing.pak").starts_with("cannot open"));
  CHECK(damage([](auto& bytes) { bytes.resize(10); }).ends_with("it is shorter than its header"));
  CHECK(damage([](auto& bytes) {
          bytes[1] = std::byte{'X'};
        }).ends_with("it does not start with the .pak magic"));
  CHECK(damage([](auto& bytes) {
          putU32(bytes, 0x08, 3);
        }).ends_with("version 3 is not supported (this build reads versions 1 to 2)"));
  CHECK(damage([](auto& bytes) {
          bytes.pop_back();
        }).ends_with("the manifest does not end the file"));
  CHECK(damage([](auto& bytes) {
          putU32(bytes, 0x10, 1000);
        }).ends_with("the names do not follow the table of contents"));
  CHECK(damage([&](auto& bytes) {
          putU32(bytes, tocOffset + 0x20, 2);
        }).ends_with("entry 0 uses compression 2, which version 2 does not have"));
  // Version 1 has no compression, so its readme cannot be compressed.
  CHECK(damage([&](auto& bytes) {
          putU32(bytes, 0x08, 1);
        }).ends_with("entry 4 uses compression 1, which version 1 does not have"));
  CHECK(damage([&](auto& bytes) {
          putU32(bytes, tocOffset + (4 * PakEntrySize) + 0x14, 1);
        }).ends_with("entry 4 expands to 4294967396 bytes, more than a .pak entry may"));
  CHECK(damage([&](auto& bytes) {
          putU32(bytes, tocOffset + 0x10, 7);
        }).ends_with("entry 0 is stored uncompressed but its sizes differ"));
  CHECK(damage([&](auto& bytes) {
          putU32(bytes, tocOffset + 0x00, 0xFFFF'FFF0);
        }).ends_with("entry 0 lies outside the data"));
  CHECK(damage([&](auto& bytes) {
          putU32(bytes, tocOffset + 0x1C, 0xFFFF);
        }).ends_with("the name of entry 0 lies outside the names"));
  // Swaps the names of the first two entries, which have the same length.
  CHECK(
      damage([&](auto& bytes) {
        putU32(bytes, tocOffset + 0x18, 24);
        putU32(bytes, tocOffset + PakEntrySize + 0x18, 0);
      }).ends_with("entry 1 ('map/start/background.png') is out of order or repeats an asset id"));
}

TEST_CASE("a .pak entry whose bytes changed does not read") {
  const TempFolder folder;
  const std::filesystem::path archive = writeFixturePak(folder);
  auto bytes = fileBytes(archive);
  bytes[PakHeaderSize] = std::byte{'N'};
  writeBytes(archive, bytes);
  const auto pak = PakFileSystem::open(archive);
  REQUIRE(pak.has_value());
  const auto read = (*pak)->readFile(archive / "map/start/background.png");
  REQUIRE_FALSE(read.has_value());
  CHECK(
      read.error().message.ends_with("its bytes do not match their SHA-1; the archive is damaged"));
  CHECK((*pak)->readFile(archive / "map/start/collision.yaml").has_value());
  CHECK_FALSE((*pak)->readFile(archive / "map/start/missing.png").has_value());
  CHECK_FALSE((*pak)->readFile(folder.path() / "elsewhere.png").has_value());
  CHECK_FALSE((*pak)->listFiles(folder.path()).has_value());
}

TEST_CASE("a compressed .pak entry that is damaged does not read") {
  const TempFolder folder;
  const std::filesystem::path archive = writeFixturePak(folder);
  const auto good = fileBytes(archive);
  const std::uint64_t readme = u64At(good, 0x18) + (4 * PakEntrySize);
  const std::uint64_t offset = u64At(good, readme);
  const std::uint64_t stored = u64At(good, readme + 0x08);
  const auto readError = [&](auto change) {
    auto bytes = good;
    change(bytes);
    writeBytes(archive, bytes);
    const auto pak = PakFileSystem::open(archive);
    REQUIRE(pak.has_value());
    CHECK((*pak)->readFile(archive / "map/start/collision.yaml").has_value());
    const auto read = (*pak)->readFile(archive / "readme.txt");
    REQUIRE_FALSE(read.has_value());
    return read.error().message;
  };

  CHECK(readError([&](auto& bytes) {
          bytes[offset] = std::byte{0};
        }).ends_with("it is not one zstd frame of its size; the archive is damaged"));
  // The table says the file is one byte shorter than the frame does.
  CHECK(readError([&](auto& bytes) {
          putU32(bytes, readme + 0x10, 99);
        }).ends_with("it is not one zstd frame of its size; the archive is damaged"));
  CHECK(readError([&](auto& bytes) {
          putU32(bytes, readme + 0x08, static_cast<std::uint32_t>(stored - 1));
        }).ends_with("the archive is damaged"));
  CHECK(readError([&](auto& bytes) {
          bytes[offset + stored - 1] = ~bytes[offset + stored - 1];
        }).ends_with("the archive is damaged"));
}

TEST_CASE("a compressed .pak entry that claims a huge size fails without expanding to it") {
  const TempFolder folder;
  folder.write("base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
  std::string text;
  for (int i = 0; i < 8; ++i) {
    text += "The quick brown fox jumps over the lazy dog, again and again. ";
  }
  folder.write("base/fox.txt", text);
  const std::filesystem::path archive = folder.path() / "base.pak";
  REQUIRE(writePak(DiskFileSystem{}, folder.path() / "base", archive).has_value());
  auto bytes = fileBytes(archive);
  const std::uint64_t entry = u64At(bytes, 0x18);
  const std::uint64_t offset = u64At(bytes, entry);
  REQUIRE(u32At(bytes, entry + 0x20) == static_cast<std::uint32_t>(PakCompression::Zstd));
  REQUIRE(u64At(bytes, entry + 0x08) >= 13);

  // A 13-byte zstd frame with one block of 128 KiB of zeros that says it holds much more, and a
  // table that agrees: 1 GiB, more than zstd's window limit, and 32 MiB, which decompression finds
  // short.
  const auto good = bytes;
  for (const std::uint32_t claimed : {1U << 30U, 1U << 25U}) {
    bytes = good;
    const std::array<std::uint8_t, 13> frame{0x28,
                                             0xB5,
                                             0x2F,
                                             0xFD, // magic
                                             0xA0, // single segment, 4-byte content size
                                             static_cast<std::uint8_t>(claimed & 0xFFU),
                                             static_cast<std::uint8_t>((claimed >> 8U) & 0xFFU),
                                             static_cast<std::uint8_t>((claimed >> 16U) & 0xFFU),
                                             static_cast<std::uint8_t>(claimed >> 24U),
                                             0x03,
                                             0x00,
                                             0x10, // last block, RLE, 128 KiB
                                             0x00};
    for (std::size_t i = 0; i < frame.size(); ++i) {
      bytes[offset + i] = std::byte{frame[i]};
    }
    putU32(bytes, entry + 0x08, static_cast<std::uint32_t>(frame.size()));
    putU32(bytes, entry + 0x10, claimed);
    writeBytes(archive, bytes);

    const auto pak = PakFileSystem::open(archive);
    REQUIRE(pak.has_value());
    const auto read = (*pak)->readFile(archive / "fox.txt");
    REQUIRE_FALSE(read.has_value());
    CHECK_MESSAGE(read.error().message.ends_with("the archive is damaged"), read.error().message);
  }
}

TEST_CASE("a version 1 .pak still reads") {
  const TempFolder folder;
  folder.write("old/pack.yaml", "id: old\nversion: 1.0.0\nlayer: base\n");
  folder.write("old/a.yaml", "a: 1\n");
  folder.write("old/b/c.txt", "short");
  const std::filesystem::path archive = folder.path() / "old.pak";
  const auto entries = writePak(DiskFileSystem{}, folder.path() / "old", archive);
  REQUIRE(entries.has_value());
  // Nothing this short compresses, so this is a version 1 archive but for its version.
  for (const auto& entry : *entries) {
    REQUIRE(entry.compression == PakCompression::None);
  }
  auto bytes = fileBytes(archive);
  putU32(bytes, 0x08, 1);
  writeBytes(archive, bytes);

  const auto pak = PakFileSystem::open(archive);
  REQUIRE(pak.has_value());
  const auto text = (*pak)->readFile(archive / "b/c.txt");
  REQUIRE(text.has_value());
  CHECK(text->size() == 5);
  const std::array location{PackLocation{.fileSystem = pak->get(), .root = archive}};
  const auto vfs = VirtualFileSystem::mount(location);
  REQUIRE(vfs.has_value());
  CHECK(vfs->ids().size() == 2);
}
