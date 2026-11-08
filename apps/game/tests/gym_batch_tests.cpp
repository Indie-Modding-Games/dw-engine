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
  return {.fieldIdle = true, .partnerNeedsCare = false, .partnerUnfit = false, .map = Map};
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

TEST_CASE("a hungry partner pauses a batch, which goes on once it is fed and the field is free") {
  GymBatch batch;
  batch.setSessions(3);
  batch.begin(Course, Map);
  GymBatchMoment hungry = idle();
  hungry.partnerNeedsCare = true;
  CHECK_FALSE(batch.next(hungry).has_value());
  CHECK(batch.waitingForCare());
  // The player feeds it beside the machine: the eating scene holds the field.
  GymBatchMoment eating = idle();
  eating.fieldIdle = false;
  eating.player = {120.0F, -80.0F};
  CHECK_FALSE(batch.next(eating).has_value());
  CHECK(batch.left() == 2);
  GymBatchMoment fed = idle();
  fed.player = eating.player;
  CHECK(batch.next(fed) == std::optional<std::string>{Course});
  CHECK_FALSE(batch.waitingForCare());
  CHECK(batch.left() == 1);
  CHECK(batch.stopped() == GymBatchStop::None);
}

TEST_CASE("a batch waiting for the partner stops on another map") {
  GymBatch batch;
  batch.setSessions(5);
  batch.begin(Course, Map);
  GymBatchMoment needs = idle();
  needs.partnerNeedsCare = true;
  CHECK_FALSE(batch.next(needs).has_value());
  GymBatchMoment away = needs;
  away.map = "town";
  CHECK_FALSE(batch.next(away).has_value());
  CHECK(batch.stopped() == GymBatchStop::MapChanged);
  CHECK_FALSE(batch.next(idle()).has_value());
}

TEST_CASE("a cancel, a sulking, injured or sick partner or a walk away stops a batch for good") {
  GymBatch batch;
  batch.setSessions(5);

  batch.begin(Course, Map);
  batch.cancel();
  CHECK(batch.stopped() == GymBatchStop::Cancelled);
  CHECK_FALSE(batch.next(idle()).has_value());

  batch.begin(Course, Map);
  CHECK(batch.stopped() == GymBatchStop::None);
  GymBatchMoment unfit = idle();
  unfit.partnerUnfit = true;
  CHECK_FALSE(batch.next(unfit).has_value());
  CHECK(batch.stopped() == GymBatchStop::PartnerUnfit);
  CHECK_FALSE(batch.next(idle()).has_value());

  // Where the player stands after a session is the mark; a step past the leash ends the batch.
  batch.begin(Course, Map);
  GymBatchMoment busy = idle();
  busy.fieldIdle = false;
  CHECK_FALSE(batch.next(busy).has_value());
  GymBatchMoment near = busy;
  near.player = {GymBatchLeash - 1.0F, 0.0F};
  CHECK_FALSE(batch.next(near).has_value());
  CHECK(batch.left() == 4);
  GymBatchMoment far = busy;
  far.player = {GymBatchLeash + 1.0F, 0.0F};
  CHECK_FALSE(batch.next(far).has_value());
  CHECK(batch.stopped() == GymBatchStop::WalkedAway);
  CHECK_FALSE(batch.next(idle()).has_value());
}

TEST_CASE("each session marks again where the player stands") {
  GymBatch batch;
  batch.setSessions(3);
  batch.begin(Course, Map);
  CHECK(batch.next(idle()) == std::optional<std::string>{Course});
  // The next session left the player somewhere else: that is the new mark.
  GymBatchMoment moved = idle();
  moved.player = {2000.0F, 0.0F};
  CHECK(batch.next(moved) == std::optional<std::string>{Course});
}

TEST_CASE("the number of sessions stays between one and five") {
  GymBatch batch;
  batch.setSessions(0);
  CHECK(batch.sessions() == GymSessionsMin);
  batch.setSessions(9);
  CHECK(batch.sessions() == GymSessionsMax);
}

} // namespace dwe::app
