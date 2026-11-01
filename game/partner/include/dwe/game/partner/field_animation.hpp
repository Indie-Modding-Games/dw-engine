#pragma once

#include <dwe/game/partner/state.hpp>

#include <cstdint>
#include <string_view>

namespace dwe::game::partner {

// The animations the partner stands, walks and gestures with on the field, by the names the
// partner's model gives them (models/partner.yaml).
enum class FieldAnimation : std::uint8_t {
  Idle,
  IdleTired,
  Walk,
  WalkTired,
  Run,
  Happy,
  Unhappy,
  Hurt,
  Hungry,
  Toilet,
  Sleepy,
  Tired,
  Sulking,
};

[[nodiscard]] std::string_view nameOf(FieldAnimation animation);

// Whether the animation is one of the two the partner stands with.
[[nodiscard]] bool isIdle(FieldAnimation animation);

// The happiness at which the partner's gesture shows its mood (partner.c:
// updateConditionAnimation): from `happyFrom` up it is happy, below `unhappyBelow` unhappy.
struct MoodGestures {
  std::int32_t happyFrom = 0;
  std::int32_t unhappyBelow = 0;
};

// partner.c: setPartnerIdle: tired, sleepy, injured or sick, it stands tired.
[[nodiscard]] FieldAnimation idleAnimation(const Conditions& conditions);

// partner.c: setPartnerSlowWalking: on the same conditions it walks tired.
[[nodiscard]] FieldAnimation walkAnimation(const Conditions& conditions);

// partner.c: updateConditionAnimation: the gesture of the partner's first condition, in the
// original's order, or of its mood without any; a neutral mood stands (setPartnerIdle).
[[nodiscard]] FieldAnimation conditionAnimation(const Conditions& conditions,
                                                std::int32_t happiness, MoodGestures mood);

// The status page's model (an addition: the original's stats view poses it in its first idle frame,
// game_menu.c: renderDigiviceEntity): the gesture of its first condition, in the field's order
// (conditionAnimation), else its plain idle; its mood never changes it.
[[nodiscard]] FieldAnimation statusAnimation(const Conditions& conditions);

} // namespace dwe::game::partner
