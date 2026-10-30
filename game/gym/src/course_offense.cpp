#include <dwe/game/gym/course_flow.hpp>

#include <cstdint>
#include <optional>

namespace dwe::game::gym {

namespace {

// The states of the swapping offense courses, numbered as trn_off.c's instance 6.
constexpr std::int32_t SwapPunchState = 7;
constexpr std::int32_t SwapWaitState = 8;

} // namespace

// trn_off.c: TRN_tickOffenseTraining, at the gym (instance 0); the other spots swap objects.
void CourseRun::offenseFrame(GymPorts& ports, const OffenseCourse& offense, const GymInput& input) {
  const SpeciesAnimation& punch = gym_->punchOf(ports.partnerSpecies());
  switch (state_) {
  case 0:
    ports.storeObjects(offense.objects);
    begin(ports);
    setState(1);
    break;
  case 1:
    if (ports.walk(Actor::Partner, course_->spot)) {
      startSpin();
      // animFlag 2 only stops the punch's root motion: the punch plays on.
      ports.animate(Actor::Partner, punch.animation);
      step_ = 0;
      ports.face(Actor::Partner, offense.facing);
      setState(offense.swap ? SwapPunchState : 2);
    }
    break;
  case 2:
    ++step_;
    ++counter_;
    if (step_ == punch.frames) {
      for (const Point3& particle : offense.particles) {
        ports.addParticle(particle);
      }
      step_ = 0;
      ports.playSound(gym_->soundSlot, offense.hitSound);
      setState(3);
    }
    break;
  case 3: {
    ++step_;
    ++counter_;
    if (step_ < offense.recoilFrames) {
      ports.moveObjects(offense.objects, offense.recoilMove);
    } else if (step_ < offense.reboundFrames) {
      ports.moveObjects(offense.objects,
                        ObjectMove{.x = -offense.recoilMove.x, .y = -offense.recoilMove.y});
    } else {
      // The bag shakes less as the swing goes on, one way on even frames and back on odd ones.
      const std::int32_t left = offense.swingBase - step_;
      const ObjectMove shake{.x = left / offense.swingDivisor.x,
                             .y = -left / offense.swingDivisor.y};
      ports.moveObjects(offense.objects,
                        counter_ % 2 == 0 ? shake : ObjectMove{.x = -shake.x, .y = -shake.y});
    }
    if (step_ >= offense.swingFrames) {
      step_ = 0;
      ports.animate(Actor::Partner, punch.animation);
      setState(2);
    }
    // The session can only end during a swing, as in the original.
    if (sessionOver(input)) {
      award(ports);
      ports.animate(Actor::Partner, course_->doneAnimation);
      ports.restoreObjects(offense.objects);
      ports.playSound(gym_->soundSlot, course_->endSound);
      ports.moveCamera(std::nullopt, course_->cameraSpeed);
      setState(4);
    }
    break;
  }
  case 4:
    if (counted()) {
      rest_ = 0;
      setState(5);
    }
    break;
  case 5:
    if (rested()) {
      ports.animate(Actor::Partner, offense.leaveAnimation);
      setState(6);
    }
    break;
  case 6:
    if (ports.walk(Actor::Partner, std::nullopt)) {
      finish(ports);
    }
    break;
  default:
    if (offense.swap) {
      offenseSwapFrame(ports, offense, *offense.swap, input);
    }
    break;
  }
}

// trn_off.c states 7 and 8 (instance 6) and trn2_off.c states 2 and 3: the punch, then the
// swapped objects for `frames`, then the punch again.
void CourseRun::offenseSwapFrame(GymPorts& ports, const OffenseCourse& offense,
                                 const OffenseSwap& swap, const GymInput& input) {
  const SpeciesAnimation& punch = gym_->punchOf(ports.partnerSpecies());
  const auto undo = [&] {
    ports.showObjects(swap.undoShown, true);
    ports.showObjects(swap.undoHidden, false);
  };
  if (state_ == SwapPunchState) {
    ++step_;
    ++counter_;
    if (step_ == punch.frames) {
      particles(ports, offense.particles);
      ports.playSound(gym_->soundSlot, offense.hitSound);
      ports.showObjects(swap.hitHidden, false);
      ports.showObjects(swap.hitShown, true);
      ports.restartObjects(swap.hitRestarted);
      step_ = 0;
      setState(SwapWaitState);
    }
    return;
  }
  if (state_ != SwapWaitState) {
    return;
  }
  ++counter_;
  ++step_;
  if (step_ == swap.frames) {
    undo();
    ports.animate(Actor::Partner, punch.animation);
    step_ = 0;
    setState(SwapPunchState);
  }
  // The session can only end while the objects are swapped, as in the original.
  if (sessionOver(input)) {
    undo();
    award(ports);
    ports.animate(Actor::Partner, course_->doneAnimation);
    ports.moveCamera(std::nullopt, course_->cameraSpeed);
    ports.playSound(gym_->soundSlot, course_->endSound);
    setState(4);
  }
}

} // namespace dwe::game::gym
