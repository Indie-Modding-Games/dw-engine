#pragma once

#include <cstdint>

namespace dwe::app {

// The bubble the partner shows as it starts to evolve (bubble.c: CONDITION_ICON_EVOLVING, bubble
// type 7). main/evl.c: tickEvoSequenceLoading makes it (addConditionBubble(7)) when its first wait
// ends and the ready clip starts, whatever started the evolution (natural, item, event script,
// virus, its way back or the console's evolve), since every one goes through partnerTickEvolving.
// bubble.c: tickConditionBubble steps it on each frame of the original from the one it was made
// on: it grows over frames 1 to 10, plays the evolving icon, bursts on frame 51 (frameCount >= 51
// draws CONDITION_SPRITES[10]) and is gone on the next one (frameCount > 50 removes it). Measured
// on the original: frameCount 15 forty-five vertical blanks after addConditionBubble, one step a
// frame at 20 frames a second, counting the frame it is made on.
inline constexpr std::int32_t EvolvingBubbleLastFrame = 51;

// The frame of the evolving bubble on the given frame of the ready wait (1 on the frame it is made,
// as the scene counts its waits), or 0 when it is not drawn.
[[nodiscard]] constexpr std::int32_t evolvingBubbleFrame(std::int32_t readyWaitFrame) {
  return readyWaitFrame >= 1 && readyWaitFrame <= EvolvingBubbleLastFrame ? readyWaitFrame : 0;
}

// Whether the field's partner bubble has a place over the partner: not while a care scene or a
// chest's notice holds the partner (out of its normal state it shows no need), but always while it
// evolves, whose walk up holds it too (partnerTickEvolving) and whose bubble hangs over it.
[[nodiscard]] constexpr bool partnerBubblePlaced(bool partnerHeld, bool evolving) {
  return !partnerHeld || evolving;
}

} // namespace dwe::app
