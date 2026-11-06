#pragma once

#include <dwe/core/error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dwe::formats::model {

// The original runs one animation tick per main-loop frame, and the loop waits three vertical
// blanks: 20 ticks a second.
inline constexpr std::uint32_t AnimationTicksPerSecond = 20;

// The most ticks a simulation runs before it gives up. The longest animation of the original takes
// 4501.
inline constexpr std::uint32_t MaxAnimationTicks = 0x8000;

// The most bones a simulation accepts: bone indices are signed bytes in the skeletons.
inline constexpr std::size_t MaxAnimationBones = 128;

// The local transform of a bone after a tick, in the original's units: scale 4096 = 1.0, rotation
// 4096 = 360° (the matrix is Rx·Ry·Rz), location in model units. The local matrix is [R·S |
// location].
struct BonePose {
  std::array<std::int32_t, 3> scale{};
  std::array<std::int16_t, 3> rotation{};
  std::array<std::int32_t, 3> location{};

  bool operator==(const BonePose&) const = default;
};

// A copy of a rectangle of the model's texture (eyes and mouths). x and width are in VRAM
// halfwords, y and height in rows, all from the origin of the model's texture.
struct TextureMove {
  std::uint32_t tick = 0;
  std::uint8_t srcX = 0;
  std::uint8_t srcY = 0;
  std::uint8_t width = 0;
  std::uint8_t height = 0;
  std::uint8_t dstX = 0;
  std::uint8_t dstY = 0;

  bool operator==(const TextureMove&) const = default;
};

// A sound to play. Bank 4 is the model's own sound bank; the original only plays it while the model
// is on screen.
struct SoundEvent {
  std::uint32_t tick = 0;
  std::uint8_t bank = 0;
  std::uint8_t sound = 0;

  bool operator==(const SoundEvent&) const = default;
};

// A loop the stream ran: after lastTick the original continues as from firstTick, count times in
// all (0xFF: forever).
struct AnimationLoop {
  std::uint32_t firstTick = 0;
  std::uint32_t lastTick = 0;
  std::uint8_t count = 0;

  bool operator==(const AnimationLoop&) const = default;
};

inline constexpr std::uint8_t LoopForever = 0xFF;

// An animation sampled at every tick, as the original plays it.
struct Animation {
  std::uint8_t id = 0;
  bool hasScale = false;
  std::uint16_t frameCount = 0;
  std::size_t boneCount = 0;
  // The pose of every bone after each tick, tick by tick. Bone 0 is the entity's root: it starts at
  // the identity.
  std::vector<BonePose> poses;
  // How far root motion has moved bone 0 after each tick, in model units, as if the entity faced
  // rotation 0.
  std::vector<std::array<double, 3>> rootMotion;
  std::vector<TextureMove> textureMoves;
  std::vector<SoundEvent> sounds;
  // Loops that jumped back, once each, in the order they first did.
  std::vector<AnimationLoop> loops;
  // The last loop repeats forever: the samples stop at its first jump back.
  bool endsInLoop = false;

  [[nodiscard]] std::uint32_t tickCount() const {
    return static_cast<std::uint32_t>(rootMotion.size());
  }
  [[nodiscard]] const BonePose& pose(std::uint32_t tick, std::size_t bone) const {
    return poses[(tick * boneCount) + bone];
  }
};

// Plays an animation stream (anim.c: startAnimation, tickAnimation in the decomp) for a skeleton of
// boneCount bones and samples it after every tick. The stream runs from the animation's offset to
// the end of the file; id is the animation's index in its table, which the original's root motion
// checks.
core::Result<Animation> simulateAnimation(std::span<const std::byte> stream, std::uint8_t id,
                                          std::size_t boneCount);

} // namespace dwe::formats::model
