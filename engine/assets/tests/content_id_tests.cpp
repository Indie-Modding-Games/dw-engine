#include <dwe/assets/content_id.hpp>

#include <doctest/doctest.h>

#include <string>

using dwe::assets::contentAssetPath;
using dwe::assets::ContentId;

TEST_CASE("an original's content id is its stable name, with no namespace") {
    const auto id = ContentId::parse("pebble");
    REQUIRE(id.has_value());
    CHECK_FALSE(id->isNamespaced());
    CHECK(id->packNamespace().empty());
    CHECK(id->name() == "pebble");
    CHECK(id->assetPath() == "pebble");
}

TEST_CASE("a pack's new content id is namespaced with the pack id and names a folder per part") {
    const auto id = ContentId::parse("my_mod:new_mon2");
    REQUIRE(id.has_value());
    CHECK(id->isNamespaced());
    CHECK(id->packNamespace() == "my_mod");
    CHECK(id->name() == "new_mon2");
    CHECK(id->str() == "my_mod:new_mon2");
    CHECK(id->assetPath() == "my_mod/new_mon2");
}

TEST_CASE("a content id refuses other characters, empty parts, two namespaces and long names") {
    for (const char* text : {"", "Pebble", "peb-ble", "a/b", ":pebble", "mod:", "a:b:c", "mod:Pebble", "Mod:pebble"}) {
        CAPTURE(text);
        CHECK_FALSE(ContentId::parse(text).has_value());
    }
    CHECK(ContentId::parse(std::string(dwe::assets::MaxContentNameLength, 'a')).has_value());
    CHECK_FALSE(ContentId::parse(std::string(dwe::assets::MaxContentNameLength + 1, 'a')).has_value());
    CHECK_FALSE(ContentId::parse(std::string(65, 'm') + ":pebble").has_value());
}

TEST_CASE("the asset path of a text that is no content id is the text itself") {
    CHECK(contentAssetPath("mod:newmap") == "mod/newmap");
    CHECK(contentAssetPath("twna01") == "twna01");
    CHECK(contentAssetPath("model.glb") == "model.glb");
}
