#include <dwe/assets/file_system.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "temp_folder.hpp"

using dwe::assets::DiskFileSystem;
using dwe::assets::MemoryFileSystem;
using dwe::assets::testing::TempFolder;

namespace {

std::string textOf(const std::vector<std::byte>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::vector<std::string> sorted(std::vector<std::string> files) {
    std::ranges::sort(files);
    return files;
}

} // namespace

TEST_CASE("the disk file system lists files recursively and skips hidden ones") {
    const TempFolder folder;
    folder.write("pack.yaml", "id: x");
    folder.write("map/a/background.png", "png");
    folder.write("map/a/.notes.txt", "hidden file");
    folder.write(".git/config", "hidden folder");
    folder.write("empty/.keep", "");

    const DiskFileSystem disk;
    const auto files = disk.listFiles(folder.path());
    REQUIRE_MESSAGE(files.has_value(), files.error().message);
    CHECK(sorted(*files) == std::vector<std::string>{"map/a/background.png", "pack.yaml"});

    const auto bytes = disk.readFile(folder.path() / "map/a/background.png");
    REQUIRE(bytes.has_value());
    CHECK(textOf(*bytes) == "png");
    CHECK(textOf(*disk.readFile(folder.path() / "empty/.keep")).empty());
}

TEST_CASE("the disk file system reports missing files and folders") {
    const TempFolder folder;
    const DiskFileSystem disk;
    CHECK(disk.listFiles(folder.path() / "missing").error().message.starts_with("cannot open folder"));
    CHECK(disk.readFile(folder.path() / "missing.txt").error().message.starts_with("cannot read"));
}

TEST_CASE("the disk file system refuses symbolic links") {
    const TempFolder folder;
    folder.write("real.txt", "text");
    std::error_code error;
    std::filesystem::create_symlink(folder.path() / "real.txt", folder.path() / "link.txt", error);
    if (error) {
        MESSAGE("symbolic links are not available here: " << error.message());
        return;
    }
    const auto files = DiskFileSystem{}.listFiles(folder.path());
    REQUIRE_FALSE(files.has_value());
    CHECK(files.error().message.ends_with("link.txt' is a symbolic link"));
}

TEST_CASE("the memory file system behaves like a folder tree") {
    MemoryFileSystem memory;
    memory.addFile("packs/base/pack.yaml", "id: base");
    memory.addFile("packs/base/map/a.png", "old");
    memory.addFile("packs/base/map/a.png", "new");
    memory.addFile("packs/base/.hidden/b.png", "");
    memory.addFile("packs/base2/c.png", "");

    const auto files = memory.listFiles("packs/base/");
    REQUIRE(files.has_value());
    CHECK(sorted(*files) == std::vector<std::string>{"map/a.png", "pack.yaml"});
    CHECK(textOf(*memory.readFile("packs/base/./map/a.png")) == "new");
    CHECK_FALSE(memory.readFile("packs/base/map/b.png").has_value());
    CHECK_FALSE(memory.listFiles("packs/missing").has_value());
}
