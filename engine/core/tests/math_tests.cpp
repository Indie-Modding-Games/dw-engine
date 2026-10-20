#include <dwe/core/math.hpp>

#include <doctest/doctest.h>

TEST_CASE("the original's axes turn into world space with Y up") {
    const glm::vec3 world = dwe::core::fromOriginalAxes({1.0F, 2.0F, 3.0F});
    CHECK(world.x == doctest::Approx(1.0F));
    CHECK(world.y == doctest::Approx(-2.0F));
    CHECK(world.z == doctest::Approx(-3.0F));
}

TEST_CASE("projection matrices use Vulkan's 0..1 depth range") {
    const glm::mat4 projection = glm::perspective(glm::radians(60.0F), 1.0F, 1.0F, 100.0F);
    const glm::vec4 nearPoint = projection * glm::vec4{0.0F, 0.0F, -1.0F, 1.0F};
    CHECK(nearPoint.z / nearPoint.w == doctest::Approx(0.0F));
}
