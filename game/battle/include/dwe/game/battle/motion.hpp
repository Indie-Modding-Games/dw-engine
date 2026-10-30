#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace dwe::game::battle {

// One frame of a clip's root motion (anim.c: tickMomentum, applyRootMomentum), in the fighter's own
// axes: `forward` along its facing, `side` along its model's x axis (its left).
struct RootStep {
  std::int32_t side = 0;
  std::int32_t forward = 0;

  friend bool operator==(const RootStep&, const RootStep&) = default;
};

// A clip of a fighter's model as the original plays it, one entry a frame: steps[k] moves the
// fighter on the clip's frame k (0 the frame it starts). A clip that repeats forever goes on from
// repeatFrom after its last frame; any other ends there (anim.c: tickAnimation clears animFlag
// bit 0), so its length is steps.size().
struct ClipMotion {
  std::vector<RootStep> steps;
  std::optional<std::size_t> repeatFrom;

  [[nodiscard]] std::size_t length() const { return steps.size(); }
  // The step of frame `frame`; nothing once a clip that does not repeat has ended.
  [[nodiscard]] std::optional<RootStep> stepAt(std::size_t frame) const {
    if (frame < steps.size()) {
      return steps[frame];
    }
    if (!repeatFrom || *repeatFrom >= steps.size()) {
      return std::nullopt;
    }
    const std::size_t period = steps.size() - *repeatFrom;
    return steps[*repeatFrom + ((frame - steps.size()) % period)];
  }
  [[nodiscard]] bool ended(std::size_t frame) const { return !repeatFrom && frame >= steps.size(); }
};

// Every clip of a fighter's model, by the original's animation id (the ids ShownClip uses).
using FighterClips = std::map<std::int32_t, ClipMotion>;

} // namespace dwe::game::battle
