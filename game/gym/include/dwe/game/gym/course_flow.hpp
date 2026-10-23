#pragma once

#include <dwe/game/gym/gym_rules.hpp>
#include <dwe/game/gym/stat_countup.hpp>

#include <cstdint>
#include <optional>

namespace dwe::game::gym {

// What a session gave: the stats before it and its gains (trn_reward.c: TRN_award*TrainingGains).
struct Award {
  GymStats before{};
  GymStats gains{};
};

// What a course asks of the field, the partner, the sound and the screen. The game implements it;
// a course knows none of them.
class GymPorts {
public:
  GymPorts() = default;
  GymPorts(const GymPorts&) = delete;
  GymPorts& operator=(const GymPorts&) = delete;
  GymPorts(GymPorts&&) = delete;
  GymPorts& operator=(GymPorts&&) = delete;
  virtual ~GymPorts() = default;

  // tamerSetState(8) and partnerSetState(10) while training; tamerSetState(0) and
  // partnerSetState(1) after.
  virtual void setTraining(bool training) = 0;
  virtual void partnerAnimation(std::int32_t animation) = 0;
  // The animation stands still on its frame (animFlag 2) until released.
  virtual void holdPartnerAnimation(bool hold) = 0;
  // 4096 per turn.
  virtual void facePartner(std::int32_t angle) = 0;
  // tickEntityWalkTo: one frame of the partner's walk to a spot, or back to the player with no
  // spot; true once it is there.
  virtual bool walkPartner(std::optional<Point> spot) = 0;
  // createCameraMovement: to a spot, or back to the player with no spot.
  virtual void moveCamera(std::optional<Point> spot, std::int32_t speed) = 0;
  virtual void playSound(std::int32_t slot, std::int32_t sound) = 0;
  // storeMapObjectPosition and loadMapObjectPosition: the objects go back where they were stored.
  virtual void storeObjects(const ObjectGroup& group) = 0;
  virtual void restoreObjects(const ObjectGroup& group) = 0;
  virtual void moveObjects(const ObjectGroup& group, ObjectMove move) = 0;
  // createCloudFX and removeAllCloudFX.
  virtual void addCloud(Point at) = 0;
  virtual void removeClouds() = 0;
  // randomLimit: 0 to limit - 1.
  virtual std::int32_t randomBelow(std::int32_t limit) = 0;
  // Trains the partner with the roulette's result in tenths (0 for none).
  virtual Award award(const CourseRules& course, std::int32_t slotTenths) = 0;
};

// What the player pressed this frame.
struct GymInput {
  // Confirm or cancel newly pressed: ends the stats count at once.
  bool confirm = false;
  // Cancel pressed or held: ends the cycle early.
  bool cancel = false;
};

// One run of a course, as trn_hp.c: TRN_tickHpTraining: the partner walks to the machine, works
// until the cycle ends or the player cancels, the stats box counts the gains, and the partner walks
// back. It steps whole frames of the original.
class CourseRun {
public:
  // The rules must outlive the run.
  CourseRun(const GymRules& gym, const CourseRules& course);

  // Real seconds of a logic tick; `frameSeconds` is how long a frame of the original lasts.
  void tick(GymPorts& ports, double seconds, double frameSeconds, const GymInput& input);

  // TRAINING_COMPLETE.
  [[nodiscard]] bool finished() const { return state_ == State::Finished; }
  // The stats box while it is on screen.
  [[nodiscard]] const std::optional<StatCountup>& countup() const { return countup_; }
  [[nodiscard]] const CourseRules& course() const { return *course_; }

private:
  enum class State : std::uint8_t { Start, WalkTo, Cycle, Count, Rest, WalkBack, Finished };

  void frame(GymPorts& ports, const GymInput& input);
  void cycleFrame(GymPorts& ports, const GymInput& input);
  void cloudLine(GymPorts& ports, const CloudLine& line) const;
  void setState(State state);

  const GymRules* gym_;
  const CourseRules* course_;
  State state_ = State::Start;
  double pendingSeconds_ = 0.0;
  // MAIN_D_8013537A: frames into the cycle, then the beats left to undo on the way back.
  std::int32_t counter_ = 0;
  // MAIN_D_8013537C.
  std::int32_t rest_ = 0;
  std::optional<StatCountup> countup_;
  // An input seen during a tick that steps no frame still counts on the next frame.
  GymInput pending_;
};

} // namespace dwe::game::gym
