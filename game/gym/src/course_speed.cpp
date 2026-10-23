#include <dwe/game/gym/course_flow.hpp>

#include <optional>

namespace dwe::game::gym {

// trn_speed.c: TRN_tickSpeedTraining, at the gym (instance 0).
void CourseRun::speedFrame(GymPorts& ports, const SpeedCourse& speed, const GymInput& input) {
  switch (state_) {
  case 0:
    begin(ports);
    setState(1);
    break;
  case 1:
    if (ports.walk(Actor::Partner, course_->spot)) {
      ports.face(Actor::Partner, speed.facing);
      ports.animate(Actor::Partner, speed.runAnimation);
      setState(2);
    }
    break;
  case 2:
    ++counter_;
    // A lap: once it ran off screen, it starts across again.
    if (!ports.partnerOnScreen() && counter_ % speed.lapFrames == 0) {
      ports.placePartnerX(speed.restartX);
      ports.animate(Actor::Partner, speed.runAnimation);
    }
    if (!sessionOver(input)) {
      break;
    }
    award(ports);
    ports.animate(Actor::Partner, course_->doneAnimation);
    ports.playSound(gym_->soundSlot, course_->endSound);
    ports.moveCamera(std::nullopt, course_->cameraSpeed);
    setState(3);
    break;
  case 3:
    if (counted()) {
      rest_ = 0;
      setState(4);
    }
    break;
  case 4:
    if (rested()) {
      ports.animate(Actor::Partner, course_->walkAnimation);
      setState(5);
    }
    break;
  case 5:
    if (ports.walk(Actor::Partner, std::nullopt)) {
      finish(ports);
    }
    break;
  default:
    break;
  }
}

} // namespace dwe::game::gym
