#include <dwe/assets/dev_pack_selection.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <string_view>
#include <utility>
#include <vector>

using dwe::assets::DevPackSelection;
using dwe::assets::findDevPacks;
using dwe::assets::FoundPack;
using dwe::assets::MemoryFileSystem;
using dwe::assets::PackLocation;
using dwe::assets::selectDevPacks;
using dwe::assets::ShadowedDevPack;

namespace {

// Adds <folder>/pack.yaml with this id.
void addPack(MemoryFileSystem& memory, std::string_view folder, std::string_view id) {
    memory.addFile(std::format("{}/pack.yaml", folder), std::format("id: {}\nversion: 1.0.0\nlayer: base\n", id));
}

PackLocation at(const MemoryFileSystem& memory, const std::filesystem::path& root) {
    return {.fileSystem = &memory, .root = root, .fillsGaps = false};
}

DevPackSelection selected(const std::vector<PackLocation>& normal, const std::vector<PackLocation>& dev) {
    auto selection = selectDevPacks(normal, dev);
    REQUIRE_MESSAGE(selection.has_value(), selection.error().message);
    return std::move(*selection);
}

std::vector<std::filesystem::path> rootsOf(const std::vector<PackLocation>& packs) {
    std::vector<std::filesystem::path> roots;
    for (const PackLocation& pack : packs) {
        CHECK(pack.fillsGaps);
        roots.push_back(pack.root);
    }
    return roots;
}

} // namespace

TEST_CASE("the dev packs are those of each extracted content folder, in order") {
    MemoryFileSystem memory;
    addPack(memory, "older/base", "base");
    addPack(memory, "newer/base", "base");
    addPack(memory, "newer/extra", "extra");
    const std::vector<std::filesystem::path> folders{"newer", "older"};
    const auto found = findDevPacks(memory, folders);
    REQUIRE_MESSAGE(found.has_value(), found.error().message);
    const std::vector<FoundPack> expected{{.path = "newer/base", .kind = FoundPack::Kind::Folder},
                                          {.path = "newer/extra", .kind = FoundPack::Kind::Folder},
                                          {.path = "older/base", .kind = FoundPack::Kind::Folder}};
    CHECK(*found == expected);
    CHECK(findDevPacks(memory, {})->empty());
}

TEST_CASE("without dev packs nothing is selected") {
    MemoryFileSystem memory;
    addPack(memory, "content/game", "game");
    const DevPackSelection selection = selected({at(memory, "content/game")}, {});
    CHECK(selection.packs.empty());
    CHECK(selection.shadowed.empty());
}

TEST_CASE("a dev pack whose id no normal pack has is selected, filling gaps") {
    MemoryFileSystem memory;
    addPack(memory, "content/game", "game");
    addPack(memory, "extracted/base", "base");
    const DevPackSelection selection = selected({at(memory, "content/game")}, {at(memory, "extracted/base")});
    CHECK(rootsOf(selection.packs) == std::vector<std::filesystem::path>{"extracted/base"});
    CHECK(selection.shadowed.empty());
}

TEST_CASE("a normal pack wins over the dev pack with its id, whatever their folder names") {
    MemoryFileSystem memory;
    addPack(memory, "content/base", "base");
    addPack(memory, "extracted/base_old", "base");
    addPack(memory, "extracted/extra", "extra");
    const DevPackSelection selection =
        selected({at(memory, "content/base")}, {at(memory, "extracted/base_old"), at(memory, "extracted/extra")});
    CHECK(rootsOf(selection.packs) == std::vector<std::filesystem::path>{"extracted/extra"});
    CHECK(selection.shadowed ==
          std::vector<ShadowedDevPack>{{.id = "base", .pack = "extracted/base_old", .winner = "content/base"}});
}

TEST_CASE("the earlier of two dev packs with the same id is selected") {
    MemoryFileSystem memory;
    addPack(memory, "newer/base", "base");
    addPack(memory, "older/base", "base");
    const DevPackSelection selection = selected({}, {at(memory, "newer/base"), at(memory, "older/base")});
    CHECK(rootsOf(selection.packs) == std::vector<std::filesystem::path>{"newer/base"});
    CHECK(selection.shadowed ==
          std::vector<ShadowedDevPack>{{.id = "base", .pack = "older/base", .winner = "newer/base"}});
}

TEST_CASE("a dev pack without a valid manifest is an error") {
    MemoryFileSystem memory;
    memory.addFile("extracted/broken/pack.yaml", "id: Broken\n");
    CHECK_FALSE(selectDevPacks({}, std::vector{at(memory, "extracted/broken")}).has_value());
}
