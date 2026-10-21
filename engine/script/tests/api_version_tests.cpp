#include <dwe/script/api_version.hpp>

#include <doctest/doctest.h>

using namespace dwe;

TEST_CASE("scripts see the Lua API version that packs are checked against") {
    CHECK(script::LuaApiVersion == assets::SupportedLuaApi);
}
