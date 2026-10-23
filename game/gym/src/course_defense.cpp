#include <dwe/game/gym/course_flow.hpp>

#include <cstddef>
#include <optional>

namespace dwe::game::gym {
namespace {

// The original's states: walking to the log, the swings, walking back to it, the count, the rest
// and walking back to the tamer.
constexpr std::int32_t WalkTo = 1;
constexpr std::int32_t Swinging = 2;
constexpr std::int32_t BackToLog = 10;
constexpr std::int32_t Counting = 12;
constexpr std::int32_t Resting = 13;
constexpr std::int32_t Leaving = 14;

} // namespace

// trn_def.c: TRN_tickDefenseTraining. The original numbers each swing as a state (2 to 9); here
// they are the rules' list.
void CourseRun::defenseFrame(GymPorts& ports, const DefenseCourse& defense, const GymInput& input) {
  switch (state_) {
  case 0:
    ports.storeObjects(defense.objects);
    shift_ = 0;
    begin(ports);
    setState(WalkTo);
    break;
  case WalkTo:
    if (ports.walk(Actor::Partner, course_->spot)) {
      startSpin();
      ports.face(Actor::Partner, defense.facing);
      ports.animate(Actor::Partner, defense.workAnimation);
      swing_ = 0;
      setState(Swinging);
    }
    break;
  case Swinging: {
    ++counter_;
    if (input.cancel) {
      counter_ = course_->cycleFrames;
    }
    const DefenseSwing& swing = defense.swings.at(swing_);
    if (swing.push) {
      ports.movePartner(Point{.x = defense.push, .z = 0});
      ports.face(Actor::Partner, defense.facing);
      ports.animate(Actor::Partner, defense.workAnimation);
      if (const auto at = ports.partnerPosition()) {
        ports.addCloud(Point{.x = at->x, .z = at->z - defense.dustBehind});
      }
    }
    if (!moveObjectsTo(ports, defense.objects, swing.dx, swing.limit)) {
      break;
    }
    if (swing_ == 0) {
      // The log hits: dust, and its sound until the session ends.
      for (const Point3& particle : defense.particles) {
        ports.addParticle(particle);
      }
      // The original starts it again each round; one voice of it plays at a time.
      if (loop_) {
        ports.stopSound(*loop_);
      }
      loop_ = ports.loopSound(gym_->soundSlot, defense.loopSound);
    }
    if (++swing_ < defense.swings.size()) {
      break;
    }
    ports.restoreObjects(defense.objects);
    shift_ = 0;
    ports.animate(Actor::Partner, course_->walkAnimation);
    setState(BackToLog);
    break;
  }
  case BackToLog:
    if (input.cancel) {
      counter_ = course_->cycleFrames;
    }
    if (!ports.walk(Actor::Partner, course_->spot)) {
      break;
    }
    // Cancel only cut the swings short above: the session itself has no cancel here.
    if (sessionOver(GymInput{})) {
      award(ports);
      ports.animate(Actor::Partner, course_->doneAnimation);
      ports.playSound(gym_->soundSlot, course_->endSound);
      if (loop_) {
        ports.stopSound(*loop_);
        loop_.reset();
      }
      ports.moveCamera(std::nullopt, course_->cameraSpeed);
      setState(Counting);
    } else {
      ports.face(Actor::Partner, defense.facing);
      ports.animate(Actor::Partner, defense.workAnimation);
      swing_ = 0;
      setState(Swinging);
    }
    break;
  case Counting:
    if (counted()) {
      rest_ = 0;
      setState(Resting);
    }
    break;
  case Resting:
    if (rested()) {
      ports.animate(Actor::Partner, course_->walkAnimation);
      setState(Leaving);
    }
    break;
  case Leaving:
    if (ports.walk(Actor::Partner, std::nullopt)) {
      ports.restoreObjects(defense.objects);
      finish(ports);
    }
    break;
  default:
    break;
  }
}

} // namespace dwe::game::gym
