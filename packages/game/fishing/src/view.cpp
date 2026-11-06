#include <dwe/core/original_math.hpp>
#include <dwe/game/fishing/rules.hpp>
#include <dwe/game/fishing/view.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

#include "fishing_math.hpp"

namespace dwe::game::fishing {

namespace {

// FISH_clipLineToRect's outcodes on one axis: below the minimum, above the maximum.
constexpr int Below = 1;
constexpr int Above = 2;

int outcode(float value, float min, float max) {
  return (value < min ? Below : 0) | (value > max ? Above : 0);
}

// The colour's channels by tension, FISH_updateTension's four quarters of [-limit, limit].
constexpr std::uint8_t Full = 0xFF;

// FISH_bobberCommand: the frames each state shows by its timer (bobbing: a third of it), -1 at
// the end; floating shows frame 3.
constexpr std::int32_t FloatingFrame = 3;
constexpr std::array<std::int32_t, 8> SinkFrames{1, 2, 3, 4, 5, 4, 3, -1};
constexpr std::array<std::int32_t, 3> BobFrames{4, 3, -1};
constexpr std::int32_t BobFramesPer = 3;
constexpr std::array<std::int32_t, 5> NibbleFrames{4, 5, 4, 3, -1};
constexpr std::array<std::int32_t, 5> BiteFrames{4, 5, 6, 7, -1};
constexpr std::array<std::int32_t, 6> LostFrames{7, 6, 5, 4, 3, -1};

// The frame of a table at a timer, held at its last entry.
template <std::size_t N>
std::int32_t frameAt(const std::array<std::int32_t, N>& frames, std::int32_t step) {
  return frames.at(static_cast<std::size_t>(std::clamp(step, 0, static_cast<std::int32_t>(N) - 1)));
}

} // namespace

glm::ivec2 clampToRange(glm::ivec2 centre, const ViewRange& range) {
  return glm::clamp(centre, range.min, glm::max(range.min, range.max));
}

std::optional<glm::ivec2> panCameraTo(glm::ivec2 centre, glm::ivec2 target, const ViewRange& range,
                                      std::int32_t radius, glm::ivec2 deadZone) {
  glm::ivec2 next = target;
  for (int axis = 0; axis < 2; ++axis) {
    if (std::abs(next[axis] - centre[axis]) <= deadZone[axis]) {
      next[axis] = centre[axis];
    } else {
      next[axis] += next[axis] > centre[axis] ? -deadZone[axis] : deadZone[axis];
    }
  }
  next = clampToRange(next, range);
  if (next == centre) {
    return std::nullopt;
  }
  if (radius > 0) {
    const glm::ivec2 offset = next - centre;
    if (glm::length(glm::vec2{offset}) >= static_cast<float>(radius)) {
      // The step along the angle in the original's integer trigonometry, rounded down.
      const std::int32_t angle = core::original::ratan2(offset.y, offset.x);
      next = centre + glm::ivec2{(radius * core::original::rcos(angle)) >> FixedShift,
                                 (radius * core::original::rsin(angle)) >> FixedShift};
    }
  }
  return clampToRange(next, range);
}

std::optional<glm::ivec2> panCameraWithDPad(glm::ivec2 centre, glm::ivec2 direction,
                                            const ViewRange& range, std::int32_t step) {
  const glm::ivec2 next = clampToRange(centre + (glm::sign(direction) * step), range);
  if (next == centre) {
    return std::nullopt;
  }
  return next;
}

bool clipLineToRect(std::array<glm::vec2, 2>& points, glm::vec2 min, glm::vec2 max) {
  // x first, then y, each end against the other as it stands then.
  for (int axis = 0; axis < 2; ++axis) {
    const int other = 1 - axis;
    std::array<int, 2> code{};
    for (std::size_t i = 0; i < 2; ++i) {
      code.at(i) = outcode(points.at(i)[axis], min[axis], max[axis]);
    }
    if ((code[0] & code[1]) != 0) {
      return false;
    }
    for (std::size_t i = 0; i < 2; ++i) {
      glm::vec2& end = points.at(i);
      const glm::vec2& far = points.at(1 - i);
      for (const auto [bit, edge] : {std::pair{Below, min[axis]}, std::pair{Above, max[axis]}}) {
        if ((code.at(i) & bit) == 0) {
          continue;
        }
        end[other] =
            far[other] + ((edge - far[axis]) * (end[other] - far[other]) / (end[axis] - far[axis]));
        end[axis] = edge;
      }
    }
  }
  return true;
}

std::vector<std::array<LinePoint, 2>> fishingLine(LinePoint from, LinePoint to, glm::vec2 min,
                                                  glm::vec2 max) {
  const auto seen = [](const LinePoint& point) {
    return point.depth > 0.0F && point.depth < LineFarDepth;
  };
  if (!seen(from) || !seen(to)) {
    return {};
  }
  std::array<glm::vec2, 2> ends{from.pixel, to.pixel};
  if (!clipLineToRect(ends, min, max)) {
    return {};
  }
  // getDistance on the screen, then pieces of 64 pixels: at least one.
  const auto length = static_cast<std::int32_t>(glm::length(ends[1] - ends[0]));
  const std::int32_t pieces = (length + static_cast<std::int32_t>(LinePieceLength)) /
                              static_cast<std::int32_t>(LinePieceLength);
  std::vector<std::array<LinePoint, 2>> out;
  out.reserve(static_cast<std::size_t>(pieces));
  LinePoint previous{};
  for (std::int32_t j = 0; j <= pieces; ++j) {
    const float t = static_cast<float>(j) / static_cast<float>(pieces);
    // The clip moves only the pixels: the depths are the ends' as projected.
    const LinePoint current{.pixel = glm::mix(ends[0], ends[1], t),
                            .depth = std::lerp(from.depth, to.depth, t)};
    if (j > 0) {
      out.push_back({previous, current});
    }
    previous = current;
  }
  return out;
}

glm::u8vec3 tensionColor(std::int32_t level, std::int32_t limit) {
  if (limit <= 0) {
    return glm::u8vec3{Full};
  }
  const std::int32_t half = limit / 2;
  const auto channel = [level](Range from, std::int32_t a, std::int32_t b) {
    return static_cast<std::uint8_t>(remapClamped(level, from, {.min = a, .max = b}));
  };
  if (level >= half) {
    const Range from{.min = half, .max = limit};
    return {channel(from, Full, Full), channel(from, Full, 0), channel(from, 0, 0)};
  }
  if (level >= 0) {
    const Range from{.min = 0, .max = half};
    return {channel(from, Full, Full), channel(from, Full, Full), channel(from, Full, 0)};
  }
  if (level >= -half) {
    const Range from{.min = -half, .max = 0};
    return {channel(from, 0, Full), channel(from, Full, Full), channel(from, Full, Full)};
  }
  const Range from{.min = -limit, .max = -half};
  return {channel(from, 0, 0), channel(from, 0, Full), channel(from, Full, Full)};
}

void pushSeaSerpentTurn(std::array<float, SeaSerpentPieces>& turns, float yaw) {
  std::shift_right(turns.begin(), turns.end(), 1);
  turns.front() = yaw;
}

std::array<SeaSerpentPiece, SeaSerpentPieces>
seaSerpentBody(glm::vec3 head, const std::array<float, SeaSerpentPieces>& turns) {
  std::array<SeaSerpentPiece, SeaSerpentPieces> body{};
  glm::vec3 at = head;
  for (std::size_t i = 0; i < SeaSerpentPieces; ++i) {
    body.at(i) = {.position = at, .yaw = turns.at(i)};
    if (i < SeaSerpentPieceGaps.size()) {
      at -= forward(turns.at(i)) * SeaSerpentPieceGaps.at(i);
    }
  }
  return body;
}

void BobberAnimation::command(Command command) {
  switch (command) {
  case Command::ToHand:
    frame_ = 1;
    state_ = State::Hanging;
    break;
  case Command::Sink:
    state_ = State::Sinking;
    break;
  case Command::Nibble:
    state_ = State::Nibble;
    break;
  case Command::Bite:
    state_ = State::Bite;
    break;
  case Command::Lost:
    state_ = State::Lost;
    break;
  }
  timer_ = 0;
}

void BobberAnimation::toFloating() {
  frame_ = FloatingFrame;
  state_ = State::Floating;
  timer_ = 0;
}

void BobberAnimation::tick(bool bobs) {
  // A table's end floats the bobber again; the timer starts at 0 on the new state.
  const auto play = [this](std::int32_t frame) {
    frame_ = frame;
    if (frame < 0) {
      toFloating();
      return false;
    }
    return true;
  };
  switch (state_) {
  case State::Rest:
    frame_ = 0;
    break;
  case State::Sinking:
    if (!play(frameAt(SinkFrames, timer_))) {
      return;
    }
    break;
  case State::Floating:
    if (bobs) {
      state_ = State::Bobbing;
      timer_ = 0;
      return;
    }
    break;
  case State::Bobbing:
    if (!play(frameAt(BobFrames, timer_ / BobFramesPer))) {
      return;
    }
    break;
  case State::Nibble:
    if (!play(frameAt(NibbleFrames, timer_))) {
      return;
    }
    break;
  case State::Bite:
    // Under at the end, hidden until the fish lets go.
    frame_ = frameAt(BiteFrames, timer_);
    if (frame_ < 0) {
      state_ = State::Under;
      timer_ = 0;
      return;
    }
    break;
  case State::Lost:
    if (!play(frameAt(LostFrames, timer_))) {
      return;
    }
    break;
  case State::Hanging:
  case State::Under:
    break;
  }
  ++timer_;
}

} // namespace dwe::game::fishing
