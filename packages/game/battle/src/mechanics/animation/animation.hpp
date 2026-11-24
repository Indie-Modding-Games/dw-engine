#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/battle_context.hpp"

namespace dwe::game::battle {

// The original's animation ids for the battle, the same for every species.
inline constexpr std::int32_t DisobeyClip = 0x13;
inline constexpr std::int32_t StanceClip = 0x21;
inline constexpr std::int32_t WeakStanceClip = 0x22;
inline constexpr std::int32_t WeakWalkClip = 0x23;
inline constexpr std::int32_t WalkClip = 0x24;
inline constexpr std::int32_t BlockClip = 0x25;
inline constexpr std::int32_t HitClip = 0x28;
inline constexpr std::int32_t HitFromBehindClip = 0x29;
inline constexpr std::int32_t FaintClip = 0x2B;

// The animation each fighter shows, started as the original starts it
// (startAnimation): the stances when the battle starts, then each frame from the poses and the
// frame's events.
void startShownClips(BattleState& state);
void updateShownClips(const BattleContext& context);
// std_main.c: STD_tickBattleResultScreen: the time ran out with a winner; the loser falls at once
// (STD_faintDigimon: startAnimation 0x2b) while the time-up box comes.
void faintAtTimeUp(Fighter& fighter, FighterId id);
// battle_main.c: BTL_setupQueuedMove: the species' clip of a move (FirstMoveAnimation plus the
// move's place in the species' list); nothing for a move outside the list.
std::optional<std::int32_t> moveClipOf(const BattleContext& context, const Fighter& fighter,
                                       std::optional<MoveRef> move);
// The length in frames of one of the fighter's clips; nothing when the battle has no motion for it.
std::optional<std::int32_t> clipLength(const Fighter& fighter, std::int32_t clip);
// Whether `clip`, which the fighter must be showing, has played to its end (anim.c: animFlag bit 0
// off): false while it shows another clip, nothing when the battle has no motion for it.
std::optional<bool> clipEnded(const Fighter& fighter, std::int32_t clip);
// The root step of the fighter's clip on its next frame; nothing once the clip ended or without
// its motion.
std::optional<RootStep> nextRootStep(const Fighter& fighter);

} // namespace dwe::game::battle
