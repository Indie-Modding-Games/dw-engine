#include <dwe/game/gym/course_flow.hpp>

#include <optional>

namespace dwe::game::gym {

// trn_brain.c: TRN_tickBrainsTraining, at the gym (instance 0): the tamer walks to the
// blackboard, the partner to its desk, and the tamer teaches.
void CourseRun::brainFrame(GymPorts& ports, const BrainCourse& brain, const GymInput& input) {
  switch (state_) {
  case 0:
    ports.setTraining(true);
    ports.animate(Actor::Tamer, brain.tamerWalk);
    ports.animate(Actor::Partner, brain.partnerStand);
    ports.playSound(gym_->soundSlot, course_->startSound);
    ports.moveCamera(brain.tamerSpot, course_->cameraSpeed);
    counter_ = 0;
    setState(1);
    break;
  case 1:
    if (ports.walk(Actor::Tamer, brain.tamerSpot)) {
      ports.animate(Actor::Tamer, brain.tamerStand);
      ports.faceEachOther();
      ports.animate(Actor::Partner, course_->walkAnimation);
      setState(2);
    }
    break;
  case 2:
    if (ports.walk(Actor::Partner, course_->spot)) {
      ports.faceEachOther();
      ports.animate(Actor::Tamer, brain.tamerTeach);
      ports.animate(Actor::Partner, brain.partnerLearn);
      setState(3);
    }
    break;
  case 3:
    ++counter_;
    if (counter_ % brain.teachEveryFrames == 0) {
      ports.playSound(gym_->soundSlot, brain.teachSound);
      ports.animate(Actor::Tamer, brain.tamerTeach);
    }
    ports.holdAnimation(Actor::Tamer, true);
    if (counter_ % brain.learnEveryFrames == 0) {
      ports.animate(Actor::Partner, brain.partnerLearn);
    }
    if (!sessionOver(input)) {
      break;
    }
    award(ports);
    ports.animate(Actor::Partner, course_->doneAnimation);
    ports.moveCamera(std::nullopt, course_->cameraSpeed);
    ports.playSound(gym_->soundSlot, course_->endSound);
    setState(4);
    break;
  case 4:
    if (counted()) {
      rest_ = 0;
      setState(5);
    }
    break;
  case 5:
    // The partner stays at its desk and follows the tamer again from there.
    if (rested()) {
      ports.holdAnimation(Actor::Tamer, false);
      finish(ports);
    }
    break;
  default:
    break;
  }
}

} // namespace dwe::game::gym
