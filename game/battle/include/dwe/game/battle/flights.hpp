#pragma once

#include <dwe/assets/effect_script.hpp>
#include <dwe/game/battle/effect_runner.hpp>
#include <dwe/game/battle/state.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::game::battle {

// When and where a move's effect touches a fighter: the attack frame (0 the frame it starts,
// BTL_startEFE) and the point of the hit on the battle's ground, which the defender turns to
// (BTL_handleHitReaction, and BTL_tickAttackHits for a block). A radius or line check hits the
// body toward the attacker (BTL_calculateAttackHitPosition): no point, the battle takes the
// attacker's place as the hit lands.
struct EffectLanding {
  std::int32_t frame = 0;
  std::optional<Point> at;
  // The attack frames, up to the touch, on which the effect turns the attacker to its target
  // (battle_effect.c: BTL_faceTargetEntity, the scripts' face_target): Spit Fire does it every
  // frame until its fire leaves.
  std::vector<std::int32_t> facingTarget;

  bool operator==(const EffectLanding&) const = default;
};

// When a technique's attack reaches a fighter. In the original the hit lands when the move's effect
// touches the fighter (battle_effect.c: the hit checks of its scripts), so each technique flies at
// its own frame and speed. The battle asks as the attack leaves; without an answer it uses the
// ruleset's projectile speed.
class AttackFlights {
public:
  AttackFlights() = default;
  AttackFlights(const AttackFlights&) = delete;
  AttackFlights& operator=(const AttackFlights&) = delete;
  AttackFlights(AttackFlights&&) = delete;
  AttackFlights& operator=(AttackFlights&&) = delete;
  virtual ~AttackFlights() = default;

  // Where the effect of `move` that `attacker` plays with its clip `clip` first touches `defender`,
  // both where they stand now; nothing when it never does or the move has no effect.
  [[nodiscard]] virtual std::optional<EffectLanding>
  landing(std::string_view move, FighterId attacker, FighterId defender,
          std::optional<std::int32_t> clip) const = 0;
};

// Where a fighter's model puts its nodes on a frame of one of its clips, for the flight probe:
// the effects start from the attacker's bones as its attack clip poses them.
class FlightPoses {
public:
  FlightPoses() = default;
  FlightPoses(const FlightPoses&) = delete;
  FlightPoses& operator=(const FlightPoses&) = delete;
  FlightPoses(FlightPoses&&) = delete;
  FlightPoses& operator=(FlightPoses&&) = delete;
  virtual ~FlightPoses() = default;

  // Node `index` of `fighter`'s model (the original's object index, as EffectBodies::node) on
  // frame `frame` of `clip`, the fighter standing at `place`, in the console's axes; nothing when
  // unknown.
  [[nodiscard]] virtual std::optional<EffectMatrix> node(FighterId fighter, std::int32_t clip,
                                                         std::int32_t frame,
                                                         const EffectBody& place,
                                                         std::int32_t index) const = 0;
};

// The flights of the techniques' effect scripts: each question runs the move's script on its own
// with the attacker and the defender where the battle has them (EffectRunner::touchBodies), frame
// by frame, until its hit checks touch the defender.
class ScriptedFlights final : public AttackFlights {
public:
  // The extracted script of a move, nothing for a move without one.
  using ScriptSource = std::function<std::optional<assets::EffectScript>(std::string_view move)>;
  // The longest flight looked for, in frames.
  static constexpr std::int32_t MaxFrames = 120;

  // `bodies`, and `poses` when given, must outlive it.
  ScriptedFlights(ScriptSource scripts, const EffectBodies& bodies,
                  const FlightPoses* poses = nullptr);

  [[nodiscard]] std::optional<EffectLanding>
  landing(std::string_view move, FighterId attacker, FighterId defender,
          std::optional<std::int32_t> clip) const override;

private:
  ScriptSource scripts_;
  const EffectBodies* bodies_;
  const FlightPoses* poses_;
  mutable std::map<std::string, std::optional<assets::EffectScript>, std::less<>> loaded_;
};

} // namespace dwe::game::battle
