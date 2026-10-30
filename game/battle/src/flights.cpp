#include <dwe/game/battle/flights.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dwe::game::battle {

namespace {

// The probe plays no sound.
class Silence final : public EffectSounds {
public:
  void play(std::size_t /*slot*/, std::int32_t /*sound*/) override {}
  std::optional<std::int64_t> playHeld(std::size_t /*slot*/, std::int32_t /*sound*/) override {
    return std::nullopt;
  }
  void stop(std::int64_t /*voice*/) override {}
};

// The two fighters of one flight as the battle has them; the attacker's bones are its attack
// clip's on the probe's frame.
class ProbeBodies final : public EffectBodies {
public:
  ProbeBodies(const EffectBodies& bodies, const FlightPoses* poses, FighterId attacker,
              FighterId defender, std::optional<std::int32_t> clip)
      : bodies_(&bodies), poses_(poses), attacker_(attacker), defender_(defender), clip_(clip) {}

  void setFrame(std::int32_t frame) { frame_ = frame; }

  [[nodiscard]] std::optional<EffectBody> body(FighterId fighter) const override {
    if (fighter != attacker_ && fighter != defender_) {
      return std::nullopt;
    }
    return bodies_->body(fighter);
  }
  [[nodiscard]] std::optional<EffectMatrix> node(FighterId fighter,
                                                 std::int32_t index) const override {
    if (fighter != attacker_ || poses_ == nullptr || !clip_) {
      return std::nullopt;
    }
    const std::optional<EffectBody> place = bodies_->body(fighter);
    if (!place) {
      return std::nullopt;
    }
    return poses_->node(fighter, *clip_, frame_, *place, index);
  }
  [[nodiscard]] std::vector<FighterId> fighters() const override {
    return attacker_ < defender_ ? std::vector{attacker_, defender_}
                                 : std::vector{defender_, attacker_};
  }
  [[nodiscard]] Point ground(const EffectVector& at) const override { return bodies_->ground(at); }

private:
  const EffectBodies* bodies_;
  const FlightPoses* poses_;
  FighterId attacker_;
  FighterId defender_;
  std::optional<std::int32_t> clip_;
  std::int32_t frame_ = 0;
};

} // namespace

ScriptedFlights::ScriptedFlights(ScriptSource scripts, const EffectBodies& bodies,
                                 const FlightPoses* poses)
    : scripts_(std::move(scripts)), bodies_(&bodies), poses_(poses) {}

std::optional<EffectLanding> ScriptedFlights::landing(std::string_view move, FighterId attacker,
                                                      FighterId defender,
                                                      std::optional<std::int32_t> clip) const {
  auto found = loaded_.find(move);
  if (found == loaded_.end()) {
    found = loaded_.emplace(std::string{move}, scripts_(move)).first;
  }
  if (!found->second || attacker == defender) {
    return std::nullopt;
  }
  Silence silence;
  ProbeBodies bodies(*bodies_, poses_, attacker, defender, clip);
  EffectRunner runner(silence);
  runner.setBodies(bodies);
  runner.touchBodies(true);
  const std::size_t slot = runner.load(*found->second);
  static_cast<void>(runner.start(slot, attacker, defender));
  for (std::int32_t frame = 0; frame < MaxFrames; ++frame) {
    bodies.setFrame(frame);
    runner.tick();
    const auto touch = std::ranges::find(runner.touched(), defender, &EffectTouch::fighter);
    if (touch != runner.touched().end()) {
      return EffectLanding{.frame = frame,
                           .at = touch->onBody ? std::nullopt
                                               : std::optional{bodies.ground(touch->at)}};
    }
    if (!runner.running(slot)) {
      break;
    }
  }
  return std::nullopt;
}

} // namespace dwe::game::battle
