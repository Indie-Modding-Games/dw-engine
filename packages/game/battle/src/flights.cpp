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

// The fighters of one flight as the battle has them: the attacker, the one its effect aims at and
// the one asked about (the same one, or a bystander on the way); the attacker's bones are its
// attack clip's on the probe's frame.
class ProbeBodies final : public EffectBodies {
public:
  ProbeBodies(const EffectBodies& bodies, const FlightPoses* poses, FighterId attacker,
              FighterId aim, FighterId touched, std::optional<std::int32_t> clip)
      : bodies_(&bodies), poses_(poses), attacker_(attacker), clip_(clip) {
    for (const FighterId fighter : {attacker, aim, touched}) {
      if (!std::ranges::contains(fighters_, fighter)) {
        fighters_.push_back(fighter);
      }
    }
    std::ranges::sort(fighters_);
  }

  void setFrame(std::int32_t frame) { frame_ = frame; }

  [[nodiscard]] std::optional<EffectBody> body(FighterId fighter) const override {
    if (!std::ranges::contains(fighters_, fighter)) {
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
  [[nodiscard]] std::vector<FighterId> fighters() const override { return fighters_; }
  [[nodiscard]] Point ground(const EffectVector& at) const override { return bodies_->ground(at); }

private:
  const EffectBodies* bodies_;
  const FlightPoses* poses_;
  FighterId attacker_;
  std::vector<FighterId> fighters_;
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
  return probe(move, attacker, defender, defender, clip);
}

std::optional<EffectLanding> ScriptedFlights::landingOn(std::string_view move, FighterId attacker,
                                                        FighterId aim, FighterId bystander,
                                                        std::optional<std::int32_t> clip) const {
  if (bystander == aim) {
    return std::nullopt;
  }
  return probe(move, attacker, aim, bystander, clip);
}

std::optional<EffectLanding> ScriptedFlights::probe(std::string_view move, FighterId attacker,
                                                    FighterId aim, FighterId touched,
                                                    std::optional<std::int32_t> clip) const {
  auto found = loaded_.find(move);
  if (found == loaded_.end()) {
    found = loaded_.emplace(std::string{move}, scripts_(move)).first;
  }
  if (!found->second || attacker == aim || attacker == touched) {
    return std::nullopt;
  }
  Silence silence;
  ProbeBodies bodies(*bodies_, poses_, attacker, aim, touched, clip);
  EffectRunner runner(silence);
  runner.setBodies(bodies);
  runner.touchBodies(true);
  const std::size_t slot = runner.load(*found->second);
  static_cast<void>(runner.start(slot, attacker, aim));
  std::vector<std::int32_t> facingTarget;
  for (std::int32_t frame = 0; frame < MaxFrames; ++frame) {
    bodies.setFrame(frame);
    const std::size_t facings = runner.targetFacings();
    runner.tick();
    // The render scripts run too, as each battle frame runs them after the ticks
    // (BTL_renderEFEEngine): some place their instance there (Magma Bomb's init_transform), which
    // a sub-effect it spawns later starts from.
    runner.render();
    if (runner.targetFacings() != facings) {
      facingTarget.push_back(frame);
    }
    const auto touch = std::ranges::find(runner.touched(), touched, &EffectTouch::fighter);
    if (touch != runner.touched().end()) {
      return EffectLanding{.frame = frame,
                           .at = touch->onBody ? std::nullopt
                                               : std::optional{bodies.ground(touch->at)},
                           .touch = touch->at,
                           .facingTarget = std::move(facingTarget)};
    }
    if (!runner.running(slot)) {
      break;
    }
  }
  return std::nullopt;
}

} // namespace dwe::game::battle
