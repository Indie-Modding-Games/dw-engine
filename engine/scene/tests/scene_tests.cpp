#include <dwe/scene/components.hpp>
#include <dwe/scene/world.hpp>

#include <doctest/doctest.h>

#include <numbers>

using dwe::scene::Motion;
using dwe::scene::Transform;
using dwe::scene::World;

TEST_CASE("the world finds entities by tag") {
    World world;
    CHECK_FALSE(world.first<dwe::scene::PlayerTag>().has_value());
    const auto player = world.create();
    world.registry().emplace<dwe::scene::PlayerTag>(player);
    world.registry().emplace<Transform>(player);
    const auto partner = world.create();
    world.registry().emplace<dwe::scene::PartnerTag>(partner);

    CHECK(world.first<dwe::scene::PlayerTag>() == player);
    CHECK(world.first<dwe::scene::PartnerTag>() == partner);
    world.destroy(partner);
    CHECK_FALSE(world.alive(partner));
    CHECK_FALSE(world.first<dwe::scene::PartnerTag>().has_value());
    world.destroy(partner);
    CHECK(world.alive(player));
}

TEST_CASE("a transform interpolates between ticks the short way") {
    constexpr float Pi = std::numbers::pi_v<float>;
    Transform transform;
    dwe::scene::place(transform, {10.0F, 0.0F, 0.0F}, Pi - 0.1F);
    dwe::scene::beginTick(transform);
    transform.position = {20.0F, 0.0F, 0.0F};
    transform.yaw = -Pi + 0.1F;

    const dwe::scene::Pose start = dwe::scene::interpolate(transform, 0.0F);
    CHECK(start.position.x == doctest::Approx(10.0F));
    const dwe::scene::Pose middle = dwe::scene::interpolate(transform, 0.5F);
    CHECK(middle.position.x == doctest::Approx(15.0F));
    // From just under pi to just over -pi is 0.2 radians through pi, not 2 pi - 0.2 the other way.
    CHECK(middle.yaw == doctest::Approx(Pi));
    CHECK(dwe::scene::interpolate(transform, 1.0F).yaw == doctest::Approx(Pi + 0.1F));
}

TEST_CASE("motions have the animation names the model data uses") {
    CHECK(dwe::scene::motionName(Motion::Idle) == "idle");
    CHECK(dwe::scene::motionName(Motion::Walk) == "walk");
    CHECK(dwe::scene::motionName(Motion::Run) == "run");
}
