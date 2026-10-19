#include <dwe/assets/asset_id.hpp>

#include <doctest/doctest.h>

#include <string>

using dwe::assets::AssetId;

TEST_CASE("asset ids accept lowercase segments separated by slashes") {
    for (const char* text : {"digimon/agumon/model", "map/file_city_01/background", "a", "x_1/2"}) {
        const auto id = AssetId::parse(text);
        REQUIRE_MESSAGE(id.has_value(), text);
        CHECK(id->str() == text);
    }
}

TEST_CASE("asset ids refuse empty segments, other characters and long names") {
    for (const char* text : {"", "/a", "a/", "a//b", "A/b", "a/b.png", "a/../b", "..", "a b", "a\\b", "a-b"}) {
        CHECK_FALSE_MESSAGE(AssetId::parse(text).has_value(), text);
    }
    CHECK(AssetId::parse(std::string(AssetId::MaxLength, 'a')).has_value());
    CHECK(AssetId::parse(std::string(AssetId::MaxLength + 1, 'a')).error().message.contains("longer than 200"));
}

TEST_CASE("asset ids order by their text") {
    CHECK(*AssetId::parse("a/b") < *AssetId::parse("a/c"));
    CHECK(*AssetId::parse("map/x") == *AssetId::parse("map/x"));
}

TEST_CASE("a file's asset id is its path without the extension") {
    CHECK(dwe::assets::assetIdOfFile("digimon/agumon/model.gltf")->str() == "digimon/agumon/model");
    CHECK(dwe::assets::assetIdOfFile("readme.txt")->str() == "readme");
    CHECK(dwe::assets::assetIdOfFile("map/file_city_01/background.ktx2")->str() == "map/file_city_01/background");
    for (const char* path : {"model", "a/model", "a/.gltf", "a/model.", "a/model.lod1.gltf", "a/model.GLTF",
                             "Map/model.png", "a//model.png", "a/mo-del.png", "a.b/model.png"}) {
        CHECK_FALSE_MESSAGE(dwe::assets::assetIdOfFile(path).has_value(), path);
    }
    CHECK(dwe::assets::assetIdOfFile("a/model.lod1.gltf").error().message ==
          "file 'a/model.lod1.gltf' must be named <name>.<extension>, with a single dot");
    CHECK(dwe::assets::assetIdOfFile("A/model.png").error().message ==
          "file 'A/model.png': asset id 'A/model' may only use a-z, 0-9, '_' and '/'");
}
