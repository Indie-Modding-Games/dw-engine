#include <dwe/assets/lua_api.hpp>

#include <doctest/doctest.h>

#include <optional>
#include <string>

using namespace dwe;

namespace {

// An engine whose API has had three minor versions.
constexpr assets::Version EngineWithMinor3{.major = 1, .minor = 3, .patch = 0};

} // namespace

TEST_CASE("scripts run on an engine with the major version of the API they need") {
    CHECK(assets::SupportedLuaApi == assets::Version{.major = 1, .minor = 0, .patch = 0});
    CHECK(assets::checkApiCompatibility("1.x"));
    CHECK(assets::checkApiCompatibility(assets::ApiRequirement{.major = 1, .minimumMinor = std::nullopt}));
    const auto newer = assets::checkApiCompatibility("2.x");
    REQUIRE_FALSE(newer.has_value());
    CHECK(newer.error().message == "the scripts need the Lua API 2.x, and this engine has 1.0.0");
    CHECK_FALSE(assets::checkApiCompatibility("0.x"));
    const auto malformed = assets::checkApiCompatibility("1.0.0");
    REQUIRE_FALSE(malformed.has_value());
    CHECK(malformed.error().message == "api '1.0.0' is not MAJOR.x or MAJOR.MINOR, such as 1.x or 1.2");
}

TEST_CASE("scripts that name a minor version need at least that one") {
    CHECK(assets::checkApiCompatibility("1.0", EngineWithMinor3));
    CHECK(assets::checkApiCompatibility("1.3", EngineWithMinor3));
    CHECK(assets::checkApiCompatibility("1.x", EngineWithMinor3));
    const auto tooNew = assets::checkApiCompatibility("1.4", EngineWithMinor3);
    REQUIRE_FALSE(tooNew.has_value());
    CHECK(tooNew.error().message == "the scripts need the Lua API 1.4, and this engine has 1.3.0");
    CHECK_FALSE(assets::checkApiCompatibility("2.x", EngineWithMinor3));
    CHECK_FALSE(assets::checkApiCompatibility("2.0", EngineWithMinor3));
    CHECK(assets::checkApiCompatibility("1.0"));
    CHECK_FALSE(assets::checkApiCompatibility("1.1"));
}

TEST_CASE("a pack that declares no API is taken to be written for this one") {
    assets::PackManifest pack;
    pack.id = "my_mod";
    CHECK(assets::checkPackApi(pack));
    pack.api = assets::ApiRequirement{.major = 3, .minimumMinor = std::nullopt};
    const auto refused = assets::checkPackApi(pack);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message == "pack 'my_mod': the scripts need the Lua API 3.x, and this engine has 1.0.0");
}
