// Batch training: a course the story starts runs several sessions in a row
// (docs/modulos/apps/game.md, «Sesiones de gimnasio»).

#include <doctest/doctest.h>

#include <optional>
#include <string>

#include "gym_batch.hpp"

namespace dwe::app {
namespace {

constexpr std::string_view Course = "speed";
constexpr std::string_view Map = "gym-map";

GymBatchMoment idle() {
  return {.fieldIdle = true, .partnerNeedsCare = false, .map = Map};
}

} // namespace

TEST_CASE("at the original's one session a course never starts again") {
  GymBatch batch;
  batch.begin(Course, Map);
  CHECK(batch.left() == 0);
  CHECK_FALSE(batch.next(idle()).has_value());
}

TEST_CASE("a batch runs its course again each time the field is idle, as many times as asked") {
  GymBatch batch;
  batch.setSessions(3);
  batch.begin(Course, Map);
  CHECK(batch.left() == 2);
  CHECK(batch.next(idle()) == std::optional<std::string>{Course});
  CHECK(batch.next(idle()) == std::optional<std::string>{Course});
  CHECK_FALSE(batch.next(idle()).has_value());
}

TEST_CASE("a batch waits while something holds the field") {
  GymBatch batch;
  batch.setSessions(2);
  batch.begin(Course, Map);
  GymBatchMoment busy = idle();
  busy.fieldIdle = false;
  CHECK_FALSE(batch.next(busy).has_value());
  CHECK(batch.left() == 1);
  CHECK(batch.next(idle()) == std::optional<std::string>{Course});
}

TEST_CASE("a cancel, a need of the partner's or another map stops a batch for good") {
  GymBatch batch;
  batch.setSessions(5);

  batch.begin(Course, Map);
  batch.cancel();
  CHECK_FALSE(batch.next(idle()).has_value());

  batch.begin(Course, Map);
  GymBatchMoment needs = idle();
  needs.partnerNeedsCare = true;
  needs.fieldIdle = false;
  CHECK_FALSE(batch.next(needs).has_value());
  CHECK_FALSE(batch.next(idle()).has_value());

  batch.begin(Course, Map);
  GymBatchMoment away = idle();
  away.map = "town";
  CHECK_FALSE(batch.next(away).has_value());
  CHECK_FALSE(batch.next(idle()).has_value());
}

TEST_CASE("the number of sessions stays between one and five") {
  GymBatch batch;
  batch.setSessions(0);
  CHECK(batch.sessions() == GymSessionsMin);
  batch.setSessions(9);
  CHECK(batch.sessions() == GymSessionsMax);
}

} // namespace dwe::app
