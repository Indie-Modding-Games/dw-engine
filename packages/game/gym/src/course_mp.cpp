#include <dwe/game/gym/course_flow.hpp>

#include <optional>

namespace dwe::game::gym {

// trn_mp.c: TRN_tickMpTraining, and trn2_mp.c: TRN2_tickMpTraining (no lit part, no loop).
void CourseRun::mpFrame(GymPorts& ports, const MpCourse& mp, const GymInput& input) {
  switch (state_) {
  case 0:
    ports.storeObjects(mp.objects);
    begin(ports);
    setState(1);
    break;
  case 1:
    if (ports.walk(Actor::Partner, mp.waypoint)) {
      setState(2);
    }
    break;
  case 2:
    if (ports.walk(Actor::Partner, course_->spot)) {
      startSpin();
      ports.showObjects(mp.hidden, false);
      ports.showObjects(mp.shown, true);
      // The lit part sits over the partner's head, wherever its model puts it.
      if (const auto head = ports.partnerHeadRow()) {
        ports.moveObjects(mp.objects, ObjectMove{.x = 0, .y = *head - mp.headLift});
      }
      ports.face(Actor::Partner, mp.facing);
      ports.animate(Actor::Partner, mp.workAnimation);
      if (mp.loopSound) {
        loop_ = ports.loopSound(gym_->soundSlot, *mp.loopSound);
      }
      setState(3);
    }
    break;
  case 3:
    ++counter_;
    // trn2_mp.c: animFrame <= frameCount always holds, so the pose starts over every frame.
    if (mp.restartWork) {
      ports.animate(Actor::Partner, mp.workAnimation);
    }
    if (!sessionOver(input)) {
      break;
    }
    award(ports);
    ports.showObjects(mp.hidden, true);
    ports.showObjects(mp.shown, false);
    ports.restoreObjects(mp.objects);
    ports.moveCamera(std::nullopt, course_->cameraSpeed);
    ports.animate(Actor::Partner, mp.leaveAnimation);
    ports.playSound(gym_->soundSlot, course_->endSound);
    stopLoop(ports);
    setState(4);
    break;
  case 4:
    if (ports.walk(Actor::Partner, mp.waypoint)) {
      ports.animate(Actor::Partner, course_->doneAnimation);
      setState(5);
    }
    break;
  case 5:
    if (counted()) {
      rest_ = 0;
      setState(6);
    }
    break;
  case 6:
    // The partner stays by the machine and follows the tamer again from there.
    if (rested()) {
      finish(ports);
    }
    break;
  default:
    break;
  }
}

} // namespace dwe::game::gym
