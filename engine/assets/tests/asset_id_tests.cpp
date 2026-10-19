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
