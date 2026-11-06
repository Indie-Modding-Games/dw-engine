#pragma once

#include <dwe/core/math.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// The fishing's marks and ripples (fish.c): the mark over a fish that rolls a bite, and the
// ripples and splashes of the water, each a pool of 16 that the fishing ticks once a frame
// (FISH_tickFishingWorld) and the caller draws with the map's fishing image (FISH_SPRITES).
namespace dwe::game::fishing {

inline constexpr std::size_t EffectPoolSize = 16;

// A rectangle of the map's fishing image (ETCDAT/FISHING.TIM or FISHING2.TIM, FISH_SPRITES): its
// top-left texel in the image, its size in texels, the texel drawn on the sprite's point and the
// palette (the CLUT's row of the image).
struct FishSprite {
  glm::ivec2 texel{0};
  glm::ivec2 size{0};
  glm::ivec2 origin{0};
  std::int32_t palette = 0;
};

// FISH_spawnMarker: a fish that rolled a bite. It lives 10 frames, and FISH_tickMarkers runs in
// the frame it appears, so it is drawn for 9 (measured: 10 at the roll, 9 at the end of that
// frame, 8 one frame later).
struct FishMarker {
  glm::vec3 position{0.0F};
  // Its chance was above 0: a heart (0x2f); else a cross (0x30).
  bool mayBite = false;
  std::int32_t lifetime = 0;
};

class MarkerPool {
public:
  // FISH_spawnMarker; nothing when the 16 are in use (FISH_allocMarker).
  void spawn(bool mayBite, glm::vec3 position);
  // FISH_tickMarkers: one frame off each; a mark at 0 goes.
  void tick();
  // FISH_clearMarkers: a fish took the hook.
  void clear();
  // The pool's slots in drawing order (FISH_drawMarkers); empty ones are nullopt.
  [[nodiscard]] const std::array<std::optional<FishMarker>, EffectPoolSize>& slots() const {
    return slots_;
  }
  [[nodiscard]] std::size_t count() const;

private:
  std::array<std::optional<FishMarker>, EffectPoolSize> slots_{};
  std::size_t next_ = 0;
};

// FISH_spawnRipple's kinds: 0 a splash (a landed fish, the bait, a fight), the others the
// bobber's rings (-1 sinking, -2 and -3 nibbles, -4 a bite).
struct Ripple {
  glm::vec3 position{0.0F};
  std::int32_t kind = 0;
  std::int32_t frame = 0;
  // The step of its animation (FISH_tickRipple): 0 not drawn yet.
  std::int32_t sprite = 0;
  // Its size in 4096ths, times the map's scale (FISH_drawRipple).
  std::int32_t scale = 0;
};

class RipplePool {
public:
  // FISH_spawnRipple's slot: the index of the new ripple, or nothing when the 16 are in use (and
  // then no spread is rolled either). A ripple that scares (scareRadius above 0) becomes the
  // pool's attractor; the caller adds the spread and puts it on the water (y 0).
  std::optional<std::size_t> spawn(std::int32_t kind, glm::vec3 position, float scareRadius,
                                   std::int32_t scale);
  // FISH_tickRipples: every ripple a step on; the attractor goes with its ripple.
  void tick();
  // The ripple a spawn returned, to move it by its spread.
  [[nodiscard]] Ripple& at(std::size_t index) { return *slots_.at(index); }
  [[nodiscard]] const std::array<std::optional<Ripple>, EffectPoolSize>& slots() const {
    return slots_;
  }
  // The last scaring ripple while it lives, and how far it scares (checkAttractor).
  [[nodiscard]] const Ripple* attractor() const;
  [[nodiscard]] float attractRadius() const { return attractRadius_; }

private:
  std::array<std::optional<Ripple>, EffectPoolSize> slots_{};
  std::size_t next_ = 0;
  std::optional<std::size_t> attractor_;
  float attractRadius_ = 0.0F;
};

// FISH_tickRipple: the next step of a ripple's animation (sets sprite and advances frame); false
// when the ripple ends.
[[nodiscard]] bool stepRipple(Ripple& ripple);

// FISH_drawMarker's and FISH_drawRipple's rectangles of the fishing image; nothing for a ripple
// not drawn this frame (sprite 0).
[[nodiscard]] FishSprite markerSprite(const FishMarker& marker);
[[nodiscard]] std::optional<FishSprite> rippleSprite(const Ripple& ripple);

// FISH_drawRipple: a ripple stands on the water as a card along world x, `scale` world units per
// texel (the map's scale times the ripple's, both in 4096ths); semi-transparent, adding (flags 5).
[[nodiscard]] float rippleUnitsPerTexel(std::int32_t mapScale, std::int32_t rippleScale);

// FISH_drawRipple sorts a ripple 10 ordering-table entries nearer than its depth: 160 world units.
inline constexpr float RippleNearer = 160.0F;

} // namespace dwe::game::fishing
