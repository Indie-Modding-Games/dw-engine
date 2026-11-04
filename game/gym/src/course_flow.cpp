#include <dwe/core/log.hpp>
#include <dwe/game/gym/course_flow.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <variant>

namespace dwe::game::gym {

CourseRun::CourseRun(const GymRules& gym, const CourseRules& course, bool withSlots)
    : gym_(&gym), course_(&course), withSlots_(withSlots) {
  core::logDebug("[port:gym] TRN_setup*Training (src/trn): course {}, roulette {}", course.id,
                 withSlots);
}

void CourseRun::tick(GymPorts& ports, double seconds, double frameSeconds, const GymInput& input) {
  pending_.confirm = pending_.confirm || input.confirm;
  pending_.cancel = pending_.cancel || input.cancel;
  pending_.cancelHeld = pending_.cancelHeld || input.cancelHeld;
  if (finished_ || frameSeconds <= 0.0) {
    return;
  }
  pendingSeconds_ += seconds;
  while (pendingSeconds_ >= frameSeconds && !finished_) {
    pendingSeconds_ -= frameSeconds;
    frame(ports, pending_);
    pending_ = {};
  }
}

void CourseRun::frame(GymPorts& ports, const GymInput& input) {
  // The box ticks on its own (trn_hud.c: TRN_tickPostTrainingStatsBox), whatever the course does.
  if (countup_.has_value() && !countup_->done()) {
    if (countup_->standing() && (input.confirm || input.cancel)) {
      countup_->finish();
    } else if (countup_->tickFrame()) {
      ports.playSound(gym_->count.soundSlot, gym_->count.sound);
    }
  }
  if (slots_.has_value()) {
    slots_->tick(ports, input.confirm);
  }
  std::visit(
      [&](const auto& detail) {
        using Detail = std::decay_t<decltype(detail)>;
        if constexpr (std::is_same_v<Detail, HpCourse>) {
          hpFrame(ports, detail, input);
        } else if constexpr (std::is_same_v<Detail, MpCourse>) {
          mpFrame(ports, detail, input);
        } else if constexpr (std::is_same_v<Detail, OffenseCourse>) {
          offenseFrame(ports, detail, input);
        } else if constexpr (std::is_same_v<Detail, DefenseCourse>) {
          defenseFrame(ports, detail, input);
        } else if constexpr (std::is_same_v<Detail, SpeedCourse>) {
          speedFrame(ports, detail, input);
        } else if constexpr (std::is_same_v<Detail, BrainCourse>) {
          brainFrame(ports, detail, input);
        } else if constexpr (std::is_same_v<Detail, DefensePulseCourse>) {
          defensePulseFrame(ports, detail, input);
        } else if constexpr (std::is_same_v<Detail, DefenseDropCourse>) {
          defenseDropFrame(ports, detail, input);
        } else {
          treadmillFrame(ports, detail, input);
        }
      },
      course_->detail);
  // tamer.c: tamerTickTraining, state 1: the player turns to the partner every frame.
  if (watching_) {
    ports.lookAt(Actor::Tamer, Actor::Partner);
  }
}

void CourseRun::setState(std::int32_t state) {
  state_ = state;
  core::logDebug("[port:gym] TRN_tick*Training (src/trn): course {}, state {}", course_->id, state);
}

void CourseRun::begin(GymPorts& ports) {
  ports.setTraining(true);
  // tamerSetState(8) (tamerTickTraining, state 0): the player stands and watches, except at the
  // brains course, which leads the player itself (tamerSetState(-1)).
  watching_ = !std::holds_alternative<BrainCourse>(course_->detail);
  if (watching_) {
    ports.animate(Actor::Tamer, gym_->watchAnimation);
  }
  ports.animate(Actor::Partner, course_->walkAnimation);
  ports.moveCamera(course_->spot, course_->cameraSpeed);
  if (course_->startSound) {
    ports.playSound(gym_->soundSlot, *course_->startSound);
  }
  counter_ = 0;
  startSlots(ports);
}

void CourseRun::startSlots(GymPorts& ports) {
  // TRN_startSlotSession (TRN2_ the same): the session's first tick opens the machine; here the
  // run holds the machine itself.
  if (withSlots_) {
    slots_.emplace(gym_->slots, course_->slotSymbol, ports);
  }
}

void CourseRun::startSpin() {
  if (slots_.has_value()) {
    slots_->startSpin();
  }
}

void CourseRun::award(GymPorts& ports) {
  // Without the roulette the default rate (TRN_getSlotSessionResult and TRN2_getSlotSessionResult
  // are not asked).
  const std::int32_t tenths =
      slots_.has_value() && slots_->result().has_value() ? *slots_->result() : 0;
  slots_.reset();
  const Award award = ports.award(*course_, tenths);
  countup_.emplace(gym_->count, award.before, award.gains);
  core::logDebug("[port:gym] TRN_award*TrainingGains (trn_reward.c): course {}", course_->id);
}

bool CourseRun::sessionOver(const GymInput& input) const {
  if (withSlots_) {
    return slots_.has_value() && slots_->result().has_value();
  }
  return counter_ >= course_->cycleFrames || input.cancel || input.cancelHeld;
}

bool CourseRun::sessionOverCounted() const {
  if (withSlots_) {
    return slots_.has_value() && slots_->result().has_value();
  }
  return counter_ >= course_->cycleFrames;
}

void CourseRun::particles(GymPorts& ports, const std::vector<Point3>& points) const {
  for (const Point3& point : points) {
    ports.addParticle(point);
  }
}

bool CourseRun::counted() const {
  return !countup_.has_value() || countup_->done();
}

bool CourseRun::rested() {
  return ++rest_ >= course_->restFrames;
}

void CourseRun::finish(GymPorts& ports) {
  // TRN_applyBaseStats (TRN2_applyBaseStats, the same) and TRN_closeUIBox: the session already gave
  // the gains, with the caps the box counts up to.
  ports.setTraining(false);
  watching_ = false;
  stopLoop(ports);
  ports.removeClouds();
  countup_.reset();
  counter_ = 0;
  finished_ = true;
  core::logDebug("[port:gym] TRAINING_COMPLETE: course {}", course_->id);
}

void CourseRun::stopLoop(GymPorts& ports) {
  if (loop_) {
    ports.stopSound(*loop_);
    loop_.reset();
  }
}

bool CourseRun::moveObjectsTo(GymPorts& ports, const ObjectGroup& group, std::int32_t dx,
                              std::int32_t limit) {
  ports.moveObjects(group, ObjectMove{.x = dx, .y = 0});
  shift_ += dx;
  return dx > 0 ? shift_ >= limit : shift_ <= limit;
}

void CourseRun::cloudLine(GymPorts& ports, const CloudLine& line) const {
  // trn_reward.c: TRN_createCloudFXLine.
  const auto* hp = std::get_if<HpCourse>(&course_->detail);
  if (hp == nullptr || counter_ % line.everyFrames != 0) {
    return;
  }
  const std::int32_t reached = std::min(counter_, course_->cycleFrames);
  const std::int32_t drift = (reached / hp->beatFrames) * line.everyFrames;
  Point at = line.start;
  for (std::int32_t i = 0; i < line.count; ++i) {
    const std::int32_t scatter = ports.randomBelow(gym_->cloudScatter);
    ports.addCloud(Point{.x = at.x + drift - scatter, .z = at.z + drift - scatter});
    at.x += line.step.x;
    at.z += line.step.z;
  }
}

// trn_hp.c: TRN_tickHpTraining.
void CourseRun::hpFrame(GymPorts& ports, const HpCourse& hp, const GymInput& input) {
  switch (state_) {
  case 0:
    ports.storeObjects(hp.objects);
    begin(ports);
    setState(1);
    break;
  case 1:
    if (ports.walk(Actor::Partner, course_->spot)) {
      startSpin();
      ports.animate(Actor::Partner, hp.workAnimation);
      ports.face(Actor::Partner, hp.facing);
      ports.showObjects(hp.shown, true);
      if (hp.arrivalSound) {
        ports.playSound(gym_->soundSlot, hp.beatSound);
      }
      setState(2);
    }
    break;
  case 2: {
    // The original sets animFlag 2 here and clears it on the beats: it only stops the clip's root
    // motion (anim.c: applyRootMomentum), which a scripted clip here never has; the clip plays on.
    if (counter_ % hp.beatFrames == 0 && counter_ <= course_->cycleFrames) {
      ports.moveObjects(hp.objects, hp.beatMove);
    }
    const std::int32_t jitter = counter_ % 2 == 0 ? hp.jitter : -hp.jitter;
    ports.moveObjects(hp.objects, ObjectMove{.x = jitter, .y = 0});
    if (counter_ % hp.beatSoundEvery == 0) {
      ports.playSound(gym_->soundSlot, hp.beatSound);
    }
    for (const CloudLine& line : hp.clouds) {
      cloudLine(ports, line);
    }
    ++counter_;
    if (!sessionOver(input)) {
      break;
    }
    counter_ %= course_->cycleFrames;
    ports.playSound(gym_->soundSlot, course_->endSound);
    counter_ /= hp.beatFrames;
    ports.animate(Actor::Partner, course_->doneAnimation);
    award(ports);
    ports.showObjects(hp.shown, false);
    ports.moveCamera(std::nullopt, course_->cameraSpeed);
    setState(3);
    break;
  }
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
  case 5: {
    // The beats are undone one frame early, as the original does; the stored positions come back
    // at the end anyway.
    if (--counter_ > 0) {
      ports.moveObjects(hp.objects, hp.returnMove);
    }
    const bool back = ports.walk(Actor::Partner, std::nullopt);
    if (back && counter_ <= 0) {
      ports.restoreObjects(hp.objects);
      finish(ports);
    }
    break;
  }
  default:
    break;
  }
}

} // namespace dwe::game::gym
