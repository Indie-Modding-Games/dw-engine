#include <dwe/core/fixed_step.hpp>

#include <doctest/doctest.h>

using dwe::core::FixedStep;

TEST_CASE("a frame shorter than a tick runs no tick and accumulates") {
    FixedStep step{0.01, 0.25};
    CHECK(step.advance(0.004) == 0);
    CHECK(step.alpha() == doctest::Approx(0.4));
}

TEST_CASE("accumulated time becomes ticks") {
    FixedStep step{0.01, 0.25};
    CHECK(step.advance(0.006) == 0);
    CHECK(step.advance(0.006) == 1);
    CHECK(step.alpha() == doctest::Approx(0.2));
}

TEST_CASE("a long frame is capped") {
    FixedStep step{0.01, 0.25};
    CHECK(step.advance(10.0) == 25);
}

TEST_CASE("negative time is ignored") {
    FixedStep step{0.01, 0.25};
    CHECK(step.advance(-1.0) == 0);
    CHECK(step.alpha() == doctest::Approx(0.0));
}

TEST_CASE("frames of exactly one tick run one tick each, without drifting") {
    FixedStep step{1.0 / 60.0, 0.25};
    int ticks = 0;
    for (int frame = 0; frame < 60 * 60; ++frame) {
        ticks += step.advance(1.0 / 60.0);
    }
    CHECK(ticks == 60 * 60);
}
