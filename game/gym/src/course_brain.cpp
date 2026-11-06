#include <dwe/game/gym/course_flow.hpp>

#include <optional>

namespace dwe::game::gym {

// trn_brain.c: TRN_tickBrainsTraining: the tamer walks to its spot, the partner to its desk, and
// the tamer teaches (the gym, instance 0) or the map's first character does (instance 9).
void CourseRun::brainFrame(GymPorts& ports, const BrainCourse& brain, const GymInput& input) {
  switch (state_) {
  case 0:
    ports.setTraining(true);
    ports.animate(Actor::Tamer, brain.tamerWalk);
    ports.animate(Actor::Partner, brain.partnerStand);
    if (course_->startSound) {
      ports.playSound(gym_->soundSlot, *course_->startSound);
    }
    ports.moveCamera(brain.tamerSpot, course_->cameraSpeed);
    counter_ = 0;
    startSlots(ports);
    setState(1);
    break;
  case 1:
    if (ports.walk(Actor::Tamer, brain.tamerSpot)) {
      // Only the tamer turns: the partner turns as it walks to its desk.
      ports.animate(Actor::Tamer, brain.tamerStand);
      ports.lookAt(Actor::Tamer, Actor::Partner);
      ports.animate(Actor::Partner, course_->walkAnimation);
      setState(2);
    }
    break;
  case 2:
    if (ports.walk(Actor::Partner, course_->spot)) {
      startSpin();
      if (brain.teacher) {
        // Instance 9: the tamer watches; the partner and the teacher face each other.
        ports.lookAt(Actor::Tamer, Actor::Partner);
        ports.lookAt(Actor::Partner, Actor::Teacher);
        ports.lookAt(Actor::Teacher, Actor::Partner);
        ports.animate(Actor::Partner, brain.partnerLearn);
        ports.animate(Actor::Teacher, brain.teacher->animation);
      } else {
        ports.faceEachOther();
        ports.animate(Actor::Tamer, brain.tamerTeach);
        ports.animate(Actor::Partner, brain.partnerLearn);
      }
      setState(3);
    }
    break;
  case 3:
    ++counter_;
    if (brain.teacher) {
      // The teacher's animation starts over once it ends (animFrame >= frameCount).
      if (ports.animationEnded(Actor::Teacher)) {
        ports.animate(Actor::Teacher, brain.teacher->animation);
      }
      if (counter_ % brain.teacher->soundEveryFrames == 0) {
        ports.playSound(gym_->soundSlot, brain.teacher->sound);
      }
    } else {
      if (counter_ % brain.teachEveryFrames == 0) {
        ports.playSound(gym_->soundSlot, brain.teachSound);
        ports.animate(Actor::Tamer, brain.tamerTeach);
      }
      // The player teaches where it stands (animFlag 2).
      ports.holdInPlace(Actor::Tamer, true);
    }
    if (counter_ % brain.learnEveryFrames == 0) {
      ports.animate(Actor::Partner, brain.partnerLearn);
    }
    if (!sessionOver(input)) {
      break;
    }
    award(ports);
    ports.animate(Actor::Partner, course_->doneAnimation);
    ports.moveCamera(std::nullopt, course_->cameraSpeed);
    if (brain.teacher) {
      ports.animate(Actor::Teacher, brain.teacher->doneAnimation);
    }
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
      finish(ports);
    }
    break;
  default:
    break;
  }
}

} // namespace dwe::game::gym
