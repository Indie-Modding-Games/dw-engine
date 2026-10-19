#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "temp_folder.hpp"

using dwe::assets::AssetId;
using dwe::assets::DiskFileSystem;
using dwe::assets::PackLocation;
using dwe::assets::PakEntrySize;
using dwe::assets::PakFileSystem;
using dwe::assets::PakHeaderSize;
using dwe::assets::VirtualFileSystem;
using dwe::assets::writePak;
using dwe::assets::testing::TempFolder;

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
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::uint64_t u64At(const std::vector<std::byte>& bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= std::to_integer<std::uint64_t>(bytes[offset + i]) << (8U * i);
    }
    return value;
}

void putU32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
    }
}

// A synthetic base pack with nested folders, an empty file and a file whose size is not a multiple of the alignment.
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
    CHECK(fromPak->resolve(*AssetId::parse("map/start/collision"))->file == archive / "map/start/collision.yaml");
}

TEST_CASE("a folder pack overrides a .pak pack below it") {
    const TempFolder folder;
    const std::filesystem::path archive = writeFixturePak(folder);
    folder.write("hd/pack.yaml", "id: hd\nversion: 0.1.0\nlayer: remaster\ndependencies:\n  - id: base\n"
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

TEST_CASE("a .pak is laid out as documented and written deterministically") {
    const TempFolder folder;
    const std::filesystem::path archive = writeFixturePak(folder);
    const auto again = writePak(DiskFileSystem{}, folder.path() / "base", folder.path() / "again.pak");
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
    }
    const std::uint64_t tocOffset = u64At(bytes, 0x18);
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
    CHECK_FALSE(writePak(DiskFileSystem{}, folder.path() / "nomanifest", folder.path() / "x.pak").has_value());
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
    CHECK(damage([](auto& bytes) { bytes[1] = std::byte{'X'}; }).ends_with("it does not start with the .pak magic"));
    CHECK(damage([](auto& bytes) {
              putU32(bytes, 0x08, 2);
          }).ends_with("version 2 is not supported (this build reads version 1)"));
    CHECK(damage([](auto& bytes) { bytes.pop_back(); }).ends_with("the manifest does not end the file"));
    CHECK(damage([](auto& bytes) {
              putU32(bytes, 0x10, 1000);
          }).ends_with("the names do not follow the table of contents"));
    CHECK(damage([&](auto& bytes) {
              putU32(bytes, tocOffset + 0x20, 1);
          }).ends_with("entry 0 uses compression 1, which this build does not read"));
    CHECK(damage([&](auto& bytes) {
              putU32(bytes, tocOffset + 0x00, 0xFFFF'FFF0);
          }).ends_with("entry 0 lies outside the data"));
    CHECK(damage([&](auto& bytes) {
              putU32(bytes, tocOffset + 0x1C, 0xFFFF);
          }).ends_with("the name of entry 0 lies outside the names"));
    // Swaps the names of the first two entries, which have the same length.
    CHECK(damage([&](auto& bytes) {
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
    CHECK(read.error().message.ends_with("its bytes do not match their SHA-1; the archive is damaged"));
    CHECK((*pak)->readFile(archive / "map/start/collision.yaml").has_value());
    CHECK_FALSE((*pak)->readFile(archive / "map/start/missing.png").has_value());
    CHECK_FALSE((*pak)->readFile(folder.path() / "elsewhere.png").has_value());
    CHECK_FALSE((*pak)->listFiles(folder.path()).has_value());
}
