#include <dwe/render/camera.hpp>
#include <dwe/render/quad_batch.hpp>

#include <doctest/doctest.h>

#include <vector>

using namespace dwe;

namespace {

glm::vec4 project(const glm::mat4& matrix, glm::vec3 point) {
    const glm::vec4 clip = matrix * glm::vec4{point, 1.0F};
    return clip / clip.w;
}

} // namespace

TEST_CASE("the pixel projection maps the target's corners to clip space, Y down") {
    const glm::mat4 projection = render::pixelProjection({.width = 640, .height = 480});
    const glm::vec4 topLeft = project(projection, {0.0F, 0.0F, 0.0F});
    const glm::vec4 bottomRight = project(projection, {640.0F, 480.0F, 0.0F});
    const glm::vec4 center = project(projection, {320.0F, 240.0F, 0.0F});
    CHECK(topLeft.x == doctest::Approx(-1.0));
    CHECK(topLeft.y == doctest::Approx(-1.0));
    CHECK(bottomRight.x == doctest::Approx(1.0));
    CHECK(bottomRight.y == doctest::Approx(1.0));
    CHECK(center.x == doctest::Approx(0.0));
    CHECK(center.y == doctest::Approx(0.0));
}

TEST_CASE("the perspective projection puts up on top of the screen and depth from 0 to 1") {
    const glm::mat4 projection = render::perspective(glm::radians(60.0F), 16.0F / 9.0F, 1.0F, 100.0F);
    const glm::vec4 near = project(projection, {0.0F, 0.0F, -1.0F});
    const glm::vec4 far = project(projection, {0.0F, 0.0F, -100.0F});
    const glm::vec4 above = project(projection, {0.0F, 1.0F, -10.0F});
    CHECK(near.z == doctest::Approx(0.0));
    CHECK(far.z == doctest::Approx(1.0));
    // Vulkan's clip space has Y down: a point above the axis lands in the upper half, at negative Y.
    CHECK(above.y < 0.0F);
}

TEST_CASE("a quad becomes two triangles covering its rectangle") {
    std::vector<render::QuadVertex> vertices;
    render::appendQuad({.position = {10.0F, 20.0F},
                        .size = {30.0F, 40.0F},
                        .uvMin = {0.0F, 0.5F},
                        .uvMax = {0.5F, 1.0F},
                        .color = {1.0F, 0.5F, 0.25F, 1.0F},
                        .texture = {.index = 7},
                        .sampler = rhi::SamplerKind::NearestRepeat},
                       vertices);
    REQUIRE(vertices.size() == 6);
    glm::vec2 min{1e9F};
    glm::vec2 max{-1e9F};
    for (const render::QuadVertex& vertex : vertices) {
        min = glm::min(min, vertex.position);
        max = glm::max(max, vertex.position);
        CHECK(vertex.texture == glm::uvec2{7, 1});
        CHECK(vertex.color == glm::vec4{1.0F, 0.5F, 0.25F, 1.0F});
        // The UV follows the position across the rectangle.
        const glm::vec2 along = (vertex.position - glm::vec2{10.0F, 20.0F}) / glm::vec2{30.0F, 40.0F};
        CHECK(vertex.uv.x == doctest::Approx(along.x * 0.5F));
        CHECK(vertex.uv.y == doctest::Approx(0.5F + (along.y * 0.5F)));
    }
    CHECK(min == glm::vec2{10.0F, 20.0F});
    CHECK(max == glm::vec2{40.0F, 60.0F});

    // Both triangles wind the same way.
    const auto winding = [&](std::size_t first) {
        const glm::vec2 a = vertices[first + 1].position - vertices[first].position;
        const glm::vec2 b = vertices[first + 2].position - vertices[first].position;
        return (a.x * b.y) - (a.y * b.x);
    };
    CHECK(winding(0) * winding(3) > 0.0F);
}
