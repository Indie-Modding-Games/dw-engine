#include <dwe/assets/virtual_file_system.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "temp_folder.hpp"

using dwe::assets::AssetId;
using dwe::assets::DiskFileSystem;
using dwe::assets::MemoryFileSystem;
using dwe::assets::VirtualFileSystem;
using dwe::assets::testing::TempFolder;

namespace {

AssetId idOf(std::string_view text) {
    auto id = AssetId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

std::string textOf(const std::vector<std::byte>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// Adds packs/<id>/pack.yaml with the given layer, priority and extra manifest lines.
void addPack(MemoryFileSystem& memory, std::string_view id, std::string_view layer, int priority = 0,
             std::string_view extra = "") {
    memory.addFile(std::format("packs/{}/pack.yaml", id),
                   std::format("id: {}\nversion: 1.2.0\nlayer: {}\npriority: {}\n{}", id, layer, priority, extra));
}

VirtualFileSystem mounted(const MemoryFileSystem& memory, const std::vector<std::filesystem::path>& roots) {
    auto vfs = VirtualFileSystem::mount(memory, roots);
    REQUIRE_MESSAGE(vfs.has_value(), vfs.error().message);
    return std::move(*vfs);
}

std::string mountError(const MemoryFileSystem& memory, const std::vector<std::filesystem::path>& roots) {
    const auto vfs = VirtualFileSystem::mount(memory, roots);
    REQUIRE_FALSE(vfs.has_value());
    return vfs.error().message;
}

std::vector<std::string> packOrder(const VirtualFileSystem& vfs) {
    std::vector<std::string> ids;
    ids.reserve(vfs.packs().size());
    for (const auto& pack : vfs.packs()) {
        ids.push_back(pack.manifest.id);
    }
    return ids;
}

std::vector<std::string> textsOf(const std::vector<AssetId>& ids) {
    std::vector<std::string> texts;
    texts.reserve(ids.size());
    for (const AssetId& id : ids) {
        texts.push_back(id.str());
    }
    return texts;
}

} // namespace

TEST_CASE("higher layers override assets by id and new ids are added") {
    MemoryFileSystem memory;
    addPack(memory, "base", "base");
    memory.addFile("packs/base/digimon/agumon/model.gltf", "base model");
    memory.addFile("packs/base/map/start/background.png", "base background");
    addPack(memory, "hd", "remaster", 0, "dependencies:\n- {id: base, min_version: 1.0.0}\n");
    memory.addFile("packs/hd/map/start/background.ktx2", "hd background");
    addPack(memory, "story", "mods");
    memory.addFile("packs/story/digimon/newcomer/model.gltf", "new model");

    const VirtualFileSystem vfs = mounted(memory, {"packs/story", "packs/base", "packs/hd"});
    CHECK(packOrder(vfs) == std::vector<std::string>{"base", "hd", "story"});
    CHECK(textsOf(vfs.ids()) ==
          std::vector<std::string>{"digimon/agumon/model", "digimon/newcomer/model", "map/start/background"});

    const auto* background = vfs.resolve(idOf("map/start/background"));
    REQUIRE(background != nullptr);
    CHECK(background->packId == "hd");
    CHECK(background->file == std::filesystem::path{"packs/hd/map/start/background.ktx2"});
    CHECK(textOf(*vfs.read(idOf("map/start/background"))) == "hd background");
    CHECK(textOf(*vfs.read(idOf("digimon/agumon/model"))) == "base model");
    CHECK(textOf(*vfs.read(idOf("digimon/newcomer/model"))) == "new model");

    CHECK(textsOf(vfs.overriddenIds()) == std::vector<std::string>{"map/start/background"});
    const auto sources = vfs.sources(idOf("map/start/background"));
    REQUIRE(sources.size() == 2);
    CHECK(sources[0].packId == "base");
    CHECK(sources[1].packId == "hd");
}

TEST_CASE("unknown assets are reported") {
    MemoryFileSystem memory;
    addPack(memory, "base", "base");
    const VirtualFileSystem vfs = mounted(memory, {"packs/base"});
    CHECK(vfs.ids().empty());
    CHECK(vfs.resolve(idOf("map/x")) == nullptr);
    CHECK(vfs.sources(idOf("map/x")).empty());
    CHECK(vfs.read(idOf("map/x")).error().message == "no pack has the asset 'map/x'");
}

TEST_CASE("packs of one layer stack by priority, then dependencies, then id") {
    MemoryFileSystem memory;
    addPack(memory, "zeta", "mods");
    addPack(memory, "alpha", "mods", 0, "dependencies:\n- {id: zeta, min_version: 1.0.0}\n");
    addPack(memory, "beta", "mods");
    addPack(memory, "low", "mods", -1);
    addPack(memory, "high", "mods", 5);
    // Priority comes first: a dependency with a higher priority stays above the pack that needs it.
    addPack(memory, "needs_high", "mods", 1, "dependencies:\n- {id: high, min_version: 1.0.0}\n");
    addPack(memory, "settings", "user", -10);

    const VirtualFileSystem vfs = mounted(memory, {"packs/settings", "packs/high", "packs/beta", "packs/alpha",
                                                   "packs/needs_high", "packs/zeta", "packs/low"});
    CHECK(packOrder(vfs) == std::vector<std::string>{"low", "beta", "zeta", "alpha", "needs_high", "high", "settings"});
}

TEST_CASE("the same packs always give the same stack") {
    MemoryFileSystem memory;
    for (const char* id : {"c", "a", "b"}) {
        addPack(memory, id, "mods");
        memory.addFile(std::format("packs/{}/shared.png", id), id);
    }
    const VirtualFileSystem first = mounted(memory, {"packs/a", "packs/b", "packs/c"});
    const VirtualFileSystem second = mounted(memory, {"packs/c", "packs/a", "packs/b"});
    CHECK(packOrder(first) == std::vector<std::string>{"a", "b", "c"});
    CHECK(packOrder(second) == packOrder(first));
    CHECK(textOf(*first.read(idOf("shared"))) == "c");
}

TEST_CASE("dependencies must be mounted, recent enough, below and without cycles") {
    MemoryFileSystem memory;
    addPack(memory, "base", "base");
    addPack(memory, "needs_missing", "mods", 0, "dependencies:\n- {id: missing, min_version: 1.0.0}\n");
    addPack(memory, "needs_newer", "mods", 0, "dependencies:\n- {id: base, min_version: 1.3.0}\n");
    addPack(memory, "mod", "mods");
    addPack(memory, "needs_mod", "remaster", 0, "dependencies:\n- {id: mod, min_version: 1.0.0}\n");
    addPack(memory, "one", "mods", 0, "dependencies:\n- {id: two, min_version: 1.0.0}\n");
    addPack(memory, "two", "mods", 3, "dependencies:\n- {id: three, min_version: 1.0.0}\n");
    addPack(memory, "three", "mods", 0, "dependencies:\n- {id: one, min_version: 1.0.0}\n");

    CHECK(mountError(memory, {"packs/needs_missing"}) ==
          "pack 'needs_missing' needs pack 'missing', which is not mounted");
    CHECK(mountError(memory, {"packs/base", "packs/needs_newer"}) ==
          "pack 'needs_newer' needs 'base' 1.3.0 or newer, but 1.2.0 is mounted");
    CHECK(mountError(memory, {"packs/mod", "packs/needs_mod"}) ==
          "pack 'needs_mod' (remaster) cannot depend on 'mod', which is in the higher layer mods");
    CHECK(mountError(memory, {"packs/three", "packs/two", "packs/one"}) ==
          "dependency cycle: one -> two -> three -> one");
}

TEST_CASE("mounting reports bad packs") {
    MemoryFileSystem memory;
    addPack(memory, "base", "base");
    memory.addFile("packs/copy/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
    memory.addFile("packs/no_manifest/a.png", "");
    memory.addFile("packs/bad_manifest/pack.yaml", "id: bad\nversion: 1\nlayer: base\n");
    addPack(memory, "ambiguous", "mods");
    memory.addFile("packs/ambiguous/digimon/x/model.gltf", "");
    memory.addFile("packs/ambiguous/digimon/x/model.glb", "");
    addPack(memory, "bad_name", "mods");
    memory.addFile("packs/bad_name/Map/a.png", "");

    CHECK(mountError(memory, {"packs/base", "packs/copy"}) ==
          "pack 'base' is mounted twice: from 'packs/base' and 'packs/copy'");
    CHECK(mountError(memory, {"packs/no_manifest"}) == "cannot read 'packs/no_manifest/pack.yaml': it does not exist");
    CHECK(mountError(memory, {"packs/bad_manifest"}) ==
          "packs/bad_manifest/pack.yaml: line 2, column 10: version '1' is not MAJOR.MINOR.PATCH");
    CHECK(mountError(memory, {"packs/ambiguous"}) == "pack 'ambiguous': 'digimon/x/model.glb' and "
                                                     "'digimon/x/model.gltf' both give the asset id 'digimon/x/model'");
    CHECK(mountError(memory, {"packs/bad_name"}) ==
          "pack 'bad_name': file 'Map/a.png': asset id 'Map/a' may only use a-z, 0-9, '_' and '/'");
}

TEST_CASE("packs mount from disk") {
    const TempFolder folder;
    folder.write("base/pack.yaml", "id: base\nversion: 1.0.0\nlayer: base\n");
    folder.write("base/map/start/background.png", "base");
    folder.write("base/map/start/collision.yaml", "cells: []\n");
    folder.write("hd/pack.yaml", "id: hd\nversion: 0.1.0\nlayer: remaster\n");
    folder.write("hd/map/start/background.png", "hd");

    const DiskFileSystem disk;
    const std::vector<std::filesystem::path> roots{folder.path() / "hd", folder.path() / "base"};
    const auto vfs = VirtualFileSystem::mount(disk, roots);
    REQUIRE_MESSAGE(vfs.has_value(), vfs.error().message);
    CHECK(textOf(*vfs->read(idOf("map/start/background"))) == "hd");
    CHECK(textOf(*vfs->read(idOf("map/start/collision"))) == "cells: []\n");
    CHECK(vfs->resolve(idOf("map/start/collision"))->file == folder.path() / "base" / "map/start/collision.yaml");
}
