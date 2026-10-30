#include <dwe/game/gym/course_flow.hpp>

#include <optional>

namespace dwe::game::gym {

// trn2_def_map108.c: TRN2_tickDefenseTrainingMap108: the partner stands guard while the scenery's
// sprites play, and takes a hit every few frames.
void CourseRun::defensePulseFrame(GymPorts& ports, const DefensePulseCourse& defense,
                                  const GymInput& input) {
  switch (state_) {
  case 0:
    begin(ports);
    setState(1);
    break;
  case 1:
    // The walk clip goes on until the first hit.
    if (ports.walk(Actor::Partner, course_->spot)) {
      ports.face(Actor::Partner, defense.facing);
      ports.showObjects(defense.hidden, false);
      ports.showObjects(defense.shown, true);
      ports.restartObjects(defense.shown);
      step_ = 0;
      setState(2);
      startSpin();
    }
    break;
  case 2:
    ++counter_;
    if (++step_ == defense.hitAfterFrames) {
      ports.animate(Actor::Partner, defense.workAnimation);
      particles(ports, defense.particles);
      ports.playSound(gym_->soundSlot, defense.hitSound);
      step_ = 0;
      setState(3);
    }
    break;
  case 3:
    // Only this state looks at cancel and the session's end.
    if (++step_ >= defense.recoverFrames) {
      ports.restartObjects(defense.shown);
      step_ = 0;
      setState(2);
    }
    if (input.cancel || input.cancelHeld) {
      counter_ = course_->cycleFrames;
    }
    ++counter_;
    if (sessionOverCounted()) {
      ports.showObjects(defense.hidden, true);
      ports.showObjects(defense.shown, false);
      award(ports);
      ports.animate(Actor::Partner, course_->doneAnimation);
      ports.moveCamera(std::nullopt, course_->cameraSpeed);
      ports.playSound(gym_->soundSlot, course_->endSound);
      setState(4);
    }
    break;
  case 4:
    if (counted()) {
      rest_ = 0;
      setState(5);
    }
    break;
  case 5:
    if (rested()) {
      ports.lookAt(Actor::Partner, Actor::Tamer);
      ports.animate(Actor::Partner, course_->walkAnimation);
      setState(6);
    }
    break;
  case 6:
    if (ports.walk(Actor::Partner, std::nullopt)) {
      finish(ports);
    }
    break;
  default:
    break;
  }
}

// trn2_def_map99.c: TRN2_tickDefenseTrainingMap99: something drops onto the partner, who faces
// the tamer on guard; only the falling frames count towards the session.
void CourseRun::defenseDropFrame(GymPorts& ports, const DefenseDropCourse& defense,
                                 const GymInput& input) {
  switch (state_) {
  case 0:
    ports.storeObjects(defense.falling);
    begin(ports);
    setState(1);
    break;
  case 1:
    if (ports.walk(Actor::Partner, course_->spot)) {
      ports.lookAt(Actor::Partner, Actor::Tamer);
      ports.animate(Actor::Partner, defense.workAnimation);
      ports.showObjects(defense.falling, true);
      step_ = 0;
      setState(2);
      startSpin();
    }
    break;
  case 2: {
    ++counter_;
    // moveMapObjectsWithLimit(.., 0, drop, 0, untilRow): down until its row reaches the limit.
    ports.moveObjects(defense.falling, ObjectMove{.x = 0, .y = defense.drop});
    const std::optional<std::int32_t> row = ports.objectRow(defense.falling.first);
    if (!row || *row >= defense.untilRow) {
      particles(ports, defense.particles);
      ports.showObjects(defense.falling, false);
      ports.showObjects(defense.hit, true);
      ports.restartObjects(defense.hit);
      step_ = 0;
      ports.playSound(gym_->soundSlot, defense.hitSound);
      setState(3);
    }
    break;
  }
  case 3:
    // animFlag 2 only stops root motion, which a scripted clip here never has.
    if (++step_ >= defense.recoverFrames) {
      ports.lookAt(Actor::Partner, Actor::Tamer);
      ports.animate(Actor::Partner, defense.workAnimation);
      ports.restoreObjects(defense.falling);
      ports.storeObjects(defense.falling);
      ports.showObjects(defense.falling, true);
      ports.showObjects(defense.hit, false);
      setState(2);
    }
    if (input.cancel || input.cancelHeld) {
      counter_ = course_->cycleFrames;
    }
    if (sessionOverCounted()) {
      award(ports);
      ports.animate(Actor::Partner, course_->doneAnimation);
      ports.moveCamera(std::nullopt, course_->cameraSpeed);
      ports.playSound(gym_->soundSlot, course_->endSound);
      setState(4);
    }
    break;
  case 4:
    if (counted()) {
      ports.showObjects(defense.falling, false);
      ports.showObjects(defense.hit, false);
      rest_ = 0;
      setState(5);
    }
    break;
  case 5:
    if (rested()) {
      ports.lookAt(Actor::Partner, Actor::Tamer);
      ports.animate(Actor::Partner, course_->walkAnimation);
      setState(6);
    }
    break;
  case 6:
    if (ports.walk(Actor::Partner, std::nullopt)) {
      ports.restoreObjects(defense.falling);
      finish(ports);
    }
    break;
  default:
    break;
  }
}

// trn2_reward.c: TRN2_tickSpeedTraining: the partner runs in place on the treadmill.
void CourseRun::treadmillFrame(GymPorts& ports, const TreadmillCourse& treadmill,
                               const GymInput& input) {
  switch (state_) {
  case 0:
    begin(ports);
    setState(1);
    break;
  case 1:
    if (ports.walk(Actor::Partner, treadmill.waypoint)) {
      loop_ = ports.loopSound(gym_->soundSlot, treadmill.loopSound);
      ports.showObjects(treadmill.shown, true);
      setState(2);
    }
    break;
  case 2:
    if (ports.walk(Actor::Partner, course_->spot)) {
      ports.face(Actor::Partner, treadmill.facing);
      ports.animate(Actor::Partner, treadmill.runAnimation);
      setState(3);
      startSpin();
    }
    break;
  case 3:
    // The run clip plays without carrying the partner (animFlag 2): it runs in place.
    ++counter_;
    if (!sessionOver(input)) {
      break;
    }
    ports.showObjects(treadmill.shown, false);
    award(ports);
    ports.animate(Actor::Partner, course_->doneAnimation);
    ports.playSound(gym_->soundSlot, course_->endSound);
    stopLoop(ports);
    ports.moveCamera(std::nullopt, course_->cameraSpeed);
    setState(4);
    break;
  case 4:
    if (counted()) {
      rest_ = 0;
      setState(5);
    }
    break;
  case 5:
    if (rested()) {
      ports.animate(Actor::Partner, course_->walkAnimation);
      setState(6);
    }
    break;
  case 6:
    if (ports.walk(Actor::Partner, treadmill.leaveSpot)) {
      finish(ports);
    }
    break;
  default:
    break;
  }
}

} // namespace dwe::game::gym
