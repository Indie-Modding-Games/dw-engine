#include <dwe/assets/pak.hpp>
#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <vector>

#include "dwx.hpp"

using dwe::assets::PakFileSystem;
using dwe::test_support::TempFolder;

TEST_CASE("dwx pack writes an archive with the pack's files") {
    const TempFolder folder;
    folder.write("mod/pack.yaml", "id: mod\nversion: 1.0.0\nlayer: mods\ndependencies:\n  - id: base\n"
                                  "    min_version: 1.0.0\n");
    folder.write("mod/map/start/collision.yaml", "cells: []\n");
    folder.write("mod/digimon/x/model.glb", "model");

    CHECK(dwe::dwx::pack(folder.path() / "mod", folder.path() / "mod/inside.pak") == dwe::dwx::ExitFailed);
    CHECK_FALSE(std::filesystem::exists(folder.path() / "mod/inside.pak"));
    REQUIRE(dwe::dwx::pack(folder.path() / "mod", folder.path() / "mod.pak") == dwe::dwx::ExitOk);

    const auto archive = PakFileSystem::open(folder.path() / "mod.pak");
    REQUIRE(archive.has_value());
    REQUIRE((*archive)->entries().size() == 2);
    CHECK((*archive)->entries()[0].path == "digimon/x/model.glb");
    const auto model = (*archive)->readFile(folder.path() / "mod.pak/digimon/x/model.glb");
    REQUIRE(model.has_value());
    CHECK(*model == std::vector{std::byte{'m'}, std::byte{'o'}, std::byte{'d'}, std::byte{'e'}, std::byte{'l'}});
}
