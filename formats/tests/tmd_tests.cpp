#include <dwe/formats/model/tmd.hpp>

#include <doctest/doctest.h>

#include <vector>

#include "model_fixtures.hpp"

using namespace dwe::formats::model;
using namespace dwe::formats::model::testing;

TEST_CASE("a TMD is read with its objects, primitives and texture fields") {
    const auto bytes = buildSampleTmd();
    const auto tmd = readTmd(bytes);
    REQUIRE(tmd.has_value());
    REQUIRE(tmd->objects.size() == 2);

    const TmdObject& first = tmd->objects[0];
    CHECK(first.vertices.size() == 4);
    CHECK(first.vertices[2] == TmdVector{0, -100, 0});
    CHECK(first.normals.size() == 2);
    REQUIRE(first.primitives.size() == 2);

    const TmdPrimitive& triangle = first.primitives[0];
    CHECK(triangle.vertexCount == 3);
    CHECK(triangle.normalCount == 3);
    CHECK(triangle.colorCount == 0);
    CHECK(triangle.isTextured());
    CHECK(triangle.isLit());
    CHECK(triangle.vertices[2] == 2);
    CHECK(triangle.normals[2] == 1);
    CHECK(triangle.uvs[1] == TmdUv{.u = 255, .v = 0});
    CHECK(triangle.uvs[2] == TmdUv{.u = 0, .v = 127});
    CHECK(triangle.cba == 0x7A40);
    CHECK(triangle.tsb == 0x0005);

    const TmdPrimitive& quad = first.primitives[1];
    CHECK(quad.vertexCount == 4);
    CHECK(quad.normalCount == 1);
    CHECK(quad.isDoubleSided());
    CHECK(quad.vertices == std::array<std::uint16_t, 4>{0, 1, 2, 3});
    CHECK(quad.tsb == 0x0025);

    const TmdObject& second = tmd->objects[1];
    REQUIRE(second.primitives.size() == 2);
    const TmdPrimitive& unlit = second.primitives[0];
    CHECK_FALSE(unlit.isLit());
    CHECK(unlit.normalCount == 0);
    CHECK(unlit.colorCount == 1);
    CHECK(unlit.colors[0] == TmdColor{.r = 10, .g = 20, .b = 30});
    CHECK(unlit.vertices == std::array<std::uint16_t, 4>{0, 1, 2, 3});

    const TmdPrimitive& gradation = second.primitives[1];
    CHECK(gradation.colorCount == 3);
    CHECK(gradation.colors[2] == TmdColor{.r = 0, .g = 0, .b = 255});
    CHECK(gradation.vertices[1] == 1);
    CHECK(gradation.vertices[2] == 2);

    REQUIRE(tmd->issues.size() == 1);
    CHECK(tmd->issues[0].object == 1);
    CHECK(tmd->issues[0].primitive == 2);
}

TEST_CASE("a primitive may use normals past its object's count while they are in the file") {
    TestObject object{
        .vertices = {0, 0, 0, 1, 0, 0, 0, 1, 0},
        .normals = {0, 0, 4096},
        .primitives = {{primitiveHeader(0x30, 0, 4), colorWord(1, 1, 1), pair(0, 0), pair(0, 1), pair(1, 2)}},
    };
    // The packet words that follow the normals are read as the second normal, as the console would.
    const auto bytes = buildTmd({object});
    const auto tmd = readTmd(bytes);
    REQUIRE(tmd.has_value());
    CHECK(tmd->objects[0].normals.size() == 2);
    CHECK(tmd->issues.size() == 1);

    object.primitives[0][4] = pair(4000, 1);
    CHECK_FALSE(readTmd(buildTmd({object})).has_value());
}

TEST_CASE("a malformed TMD is an error") {
    const auto good = buildSampleTmd();

    CHECK_FALSE(readTmd(std::span(good).first(8)).has_value());

    auto badId = good;
    badId[0] = std::byte{0x42};
    CHECK_FALSE(readTmd(badId).has_value());

    auto absolute = good;
    absolute[4] = std::byte{1};
    CHECK_FALSE(readTmd(absolute).has_value());

    auto tooManyObjects = good;
    tooManyObjects[8] = std::byte{0xFF};
    CHECK_FALSE(readTmd(tooManyObjects).has_value());

    // Object 0's vertex count, then its primitive count, pushed past the end of the file.
    auto tooManyVertices = good;
    tooManyVertices[12 + 4 + 3] = std::byte{0x10};
    CHECK_FALSE(readTmd(tooManyVertices).has_value());

    auto tooManyPrimitives = good;
    tooManyPrimitives[12 + 20 + 1] = std::byte{0x10};
    CHECK_FALSE(readTmd(tooManyPrimitives).has_value());

    TestObject badVertex{
        .vertices = {0, 0, 0},
        .normals = {0, 0, 4096},
        .primitives = {{primitiveHeader(0x20, 0, 3), colorWord(1, 1, 1), pair(0, 0), pair(0, 5)}},
    };
    CHECK_FALSE(readTmd(buildTmd({badVertex})).has_value());

    const auto truncated = std::span(good).first(good.size() - 2);
    CHECK_FALSE(readTmd(truncated).has_value());
}
