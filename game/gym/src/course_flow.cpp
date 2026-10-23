#include <dwe/core/log.hpp>
#include <dwe/game/gym/course_flow.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>

namespace dwe::game::gym {

CourseRun::CourseRun(const GymRules& gym, const CourseRules& course)
    : gym_(&gym), course_(&course) {
  core::logDebug("[port:gym] TRN_setupHpTraining (trn_hp.c:58): course {}", course.id);
}

void CourseRun::tick(GymPorts& ports, double seconds, double frameSeconds, const GymInput& input) {
  pending_.confirm = pending_.confirm || input.confirm;
  pending_.cancel = pending_.cancel || input.cancel;
  if (state_ == State::Finished || frameSeconds <= 0.0) {
    return;
  }
  pendingSeconds_ += seconds;
  while (pendingSeconds_ >= frameSeconds && state_ != State::Finished) {
    pendingSeconds_ -= frameSeconds;
    frame(ports, pending_);
    pending_ = {};
  }
}

void CourseRun::setState(State state) {
  state_ = state;
  core::logDebug("[port:gym] TRN_tickHpTraining (trn_hp.c:79): state {}", static_cast<int>(state));
}

void CourseRun::frame(GymPorts& ports, const GymInput& input) {
  const CourseRules& course = *course_;
  switch (state_) {
  case State::Start:
    ports.storeObjects(course.objects);
    ports.setTraining(true);
    ports.partnerAnimation(course.animations.walk);
    ports.moveCamera(course.spot, course.cameraSpeed);
    ports.playSound(gym_->soundSlot, course.sounds.start);
    counter_ = 0;
    setState(State::WalkTo);
    break;
  case State::WalkTo:
    if (ports.walkPartner(course.spot)) {
      ports.partnerAnimation(course.animations.work);
      ports.facePartner(course.facing);
      ports.playSound(gym_->soundSlot, course.sounds.beat);
      setState(State::Cycle);
    }
    break;
  case State::Cycle:
    cycleFrame(ports, input);
    break;
  case State::Count:
    // The box ticks on its own (trn_hud.c: TRN_tickPostTrainingStatsBox); the course waits for it.
    if (countup_.has_value() && !countup_->done()) {
      if (input.confirm) {
        countup_->finish();
      } else if (countup_->tickFrame()) {
        ports.playSound(gym_->count.soundSlot, gym_->count.sound);
      }
    }
    if (!countup_.has_value() || countup_->done()) {
      rest_ = 0;
      setState(State::Rest);
    }
    break;
  case State::Rest:
    if (++rest_ >= course.restFrames) {
      ports.partnerAnimation(course.animations.walk);
      setState(State::WalkBack);
    }
    break;
  case State::WalkBack: {
    // The beats are undone one frame early, as the original does; the stored positions come back
    // at the end anyway.
    if (--counter_ > 0) {
      ports.moveObjects(course.objects, course.returnMove);
    }
    const bool back = ports.walkPartner(std::nullopt);
    if (back && counter_ <= 0) {
      ports.holdPartnerAnimation(false);
      ports.setTraining(false);
      ports.restoreObjects(course.objects);
      ports.removeClouds();
      countup_.reset();
      counter_ = 0;
      setState(State::Finished);
    }
    break;
  }
  case State::Finished:
    break;
  }
}

void CourseRun::cycleFrame(GymPorts& ports, const GymInput& input) {
  const CourseRules& course = *course_;
  ports.holdPartnerAnimation(true);
  if (counter_ % course.beatFrames == 0 && counter_ <= course.cycleFrames) {
    ports.holdPartnerAnimation(false);
    ports.moveObjects(course.objects, course.beatMove);
  }
  const std::int32_t jitter = counter_ % 2 == 0 ? course.jitter : -course.jitter;
  ports.moveObjects(course.objects, ObjectMove{.x = jitter, .y = 0});
  if (counter_ % course.soundEveryFrames == 0) {
    ports.playSound(gym_->soundSlot, course.sounds.beat);
  }
  for (const CloudLine& line : course.clouds) {
    cloudLine(ports, line);
  }
  ++counter_;
  // The roulette (TRN_getSlotSessionResult) comes with the slot slice; without it the cycle ends
  // on time or on cancel, at the default rate.
  if (counter_ < course.cycleFrames && !input.cancel) {
    return;
  }
  counter_ %= course.cycleFrames;
  ports.playSound(gym_->soundSlot, course.sounds.end);
  counter_ /= course.beatFrames;
  ports.partnerAnimation(course.animations.done);
  const Award award = ports.award(course, 0);
  countup_.emplace(gym_->count, award.before, award.gains);
  ports.moveCamera(std::nullopt, course.cameraSpeed);
  core::logDebug("[port:gym] TRN_tickHpTraining (trn_hp.c:131): cycle over, {} beats to undo",
                 counter_);
  setState(State::Count);
}

void CourseRun::cloudLine(GymPorts& ports, const CloudLine& line) const {
  // trn_reward.c: TRN_createCloudFXLine.
  if (counter_ % line.everyFrames != 0) {
    return;
  }
  const std::int32_t reached = std::min(counter_, course_->cycleFrames);
  const std::int32_t drift = (reached / course_->beatFrames) * line.everyFrames;
  Point at = line.start;
  for (std::int32_t i = 0; i < line.count; ++i) {
    const std::int32_t scatter = ports.randomBelow(gym_->cloudScatter);
    ports.addCloud(Point{.x = at.x + drift - scatter, .z = at.z + drift - scatter});
    at.x += line.step.x;
    at.z += line.step.z;
  }
}

} // namespace dwe::game::gym
