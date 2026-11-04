#include <dwe/game/fishing/effects.hpp>

#include <algorithm>
#include <cstdlib>
#include <span>
#include <utility>

namespace dwe::game::fishing {

namespace {

// FISH_spawnMarker.
constexpr std::int32_t MarkerLifetime = 10;

// FISH_SPRITES: the rectangles of the fishing image, as u0, v0, u1, v1, ox, oy and the CLUT's row
// (0x601c is row 0, 0x611c row 4). The image starts at the texture page's v 0x8c (VRAM y 396).
struct SpriteEntry {
  std::uint8_t u0;
  std::uint8_t v0;
  std::uint8_t u1;
  std::uint8_t v1;
  std::uint8_t ox;
  std::uint8_t oy;
  std::uint8_t palette;
};
constexpr std::int32_t ImageTop = 0x8c;

// FISH_SPRITES[0..16]: the splash (0 to 4) and the bobber's rings (5 to 16).
constexpr std::array<SpriteEntry, 17> RippleSprites{{
    {0xd0, 0xb4, 0xdf, 0xc3, 0x08, 0x0a, 0},
    {0x80, 0xd4, 0x9f, 0xe3, 0x10, 0x08, 0},
    {0xa0, 0xd4, 0xbf, 0xe3, 0x10, 0x08, 0},
    {0xc0, 0xd4, 0xdf, 0xe3, 0x10, 0x08, 0},
    {0x80, 0xe4, 0x9f, 0xf3, 0x10, 0x08, 0},
    {0x80, 0xb4, 0x8f, 0xc3, 0x08, 0x08, 0},
    {0x90, 0xb4, 0x9f, 0xc3, 0x08, 0x08, 0},
    {0xa0, 0xb4, 0xaf, 0xc3, 0x08, 0x08, 0},
    {0xb0, 0xb4, 0xbf, 0xc3, 0x08, 0x08, 0},
    {0xc0, 0xb4, 0xcf, 0xc3, 0x08, 0x08, 0},
    {0x80, 0xb4, 0x8f, 0xc3, 0x08, 0x08, 0},
    {0x90, 0xc4, 0x9f, 0xd3, 0x08, 0x08, 0},
    {0xa0, 0xc4, 0xaf, 0xd3, 0x08, 0x08, 0},
    {0xb0, 0xc4, 0xbf, 0xd3, 0x08, 0x08, 0},
    {0xc0, 0xc4, 0xcf, 0xd3, 0x08, 0x08, 0},
    {0xd0, 0xc4, 0xdf, 0xd3, 0x08, 0x08, 0},
    {0xe0, 0xc4, 0xef, 0xd3, 0x08, 0x08, 0},
}};
// FISH_SPRITES[0x2f] and [0x30]: a heart and a cross.
constexpr SpriteEntry MayBiteSprite{0x68, 0x9c, 0x77, 0xab, 0x08, 0x10, 4};
constexpr SpriteEntry NoBiteSprite{0x58, 0x8c, 0x67, 0x9b, 0x08, 0x10, 4};

// FISH_tickRipple's steps per kind (MAIN_D_80134958, ...60, ...68, ...6C and FISH_D_8007A4B4):
// -1 ends the ripple. The splash shows each step for 2 frames, kind -2 for 3.
constexpr std::array<std::int32_t, 6> SplashSteps{1, 2, 3, 4, 5, -1};
constexpr std::array<std::int32_t, 7> SinkSteps{0, 1, 2, 3, 4, 5, -1};
constexpr std::array<std::int32_t, 3> SmallNibbleSteps{4, 5, -1};
constexpr std::array<std::int32_t, 5> NibbleSteps{2, 3, 4, 5, -1};
constexpr std::array<std::int32_t, 17> BiteSteps{0, 0, 6, 7, 8, 9, 10, 11, 12,
                                                 2, 3, 4, 4, 5, 5, 5,  -1};
// A step at or past this ends the ripple too.
constexpr std::int32_t StepCount = 13;
// A ring's step n is FISH_SPRITES[n + 4]; a splash's FISH_SPRITES[n - 1].
constexpr std::int32_t RingSpriteOffset = 4;

constexpr float FixedOne = 4096.0F;

FishSprite spriteOf(const SpriteEntry& entry) {
  return {.texel = {entry.u0, entry.v0 - ImageTop},
          .size = {std::abs(entry.u1 - entry.u0), std::abs(entry.v1 - entry.v0)},
          .origin = {entry.ox, entry.oy},
          .palette = entry.palette};
}

std::int32_t stepOf(std::span<const std::int32_t> steps, std::int32_t index) {
  // The original reads past a table only on a ripple that already ended.
  if (index < 0 || std::cmp_greater_equal(index, steps.size())) {
    return -1;
  }
  return steps[static_cast<std::size_t>(index)];
}

// FISH_allocMarker and FISH_allocRipple: the first free slot from the last one taken, around.
template <typename T>
std::optional<std::size_t> allocate(const std::array<std::optional<T>, EffectPoolSize>& slots,
                                    std::size_t& next) {
  for (std::size_t tried = 0; tried < EffectPoolSize; ++tried) {
    const std::size_t index = (next + tried) % EffectPoolSize;
    if (!slots.at(index)) {
      next = index;
      return index;
    }
  }
  return std::nullopt;
}

} // namespace

void MarkerPool::spawn(bool mayBite, glm::vec3 position) {
  const std::optional<std::size_t> slot = allocate(slots_, next_);
  if (!slot) {
    return;
  }
  slots_.at(*slot) =
      FishMarker{.position = position, .mayBite = mayBite, .lifetime = MarkerLifetime};
}

void MarkerPool::tick() {
  for (std::optional<FishMarker>& marker : slots_) {
    if (marker && --marker->lifetime == 0) {
      marker.reset();
    }
  }
}

void MarkerPool::clear() {
  slots_.fill(std::nullopt);
}

std::size_t MarkerPool::count() const {
  return static_cast<std::size_t>(
      std::ranges::count_if(slots_, [](const auto& slot) { return slot.has_value(); }));
}

std::optional<std::size_t> RipplePool::spawn(std::int32_t kind, glm::vec3 position,
                                             float scareRadius, std::int32_t scale) {
  const std::optional<std::size_t> slot = allocate(slots_, next_);
  if (!slot) {
    return std::nullopt;
  }
  slots_.at(*slot) =
      Ripple{.position = position, .kind = kind, .frame = 0, .sprite = 0, .scale = scale};
  if (scareRadius != 0.0F) {
    attractor_ = *slot;
    attractRadius_ = scareRadius;
  }
  return slot;
}

void RipplePool::tick() {
  for (std::optional<Ripple>& ripple : slots_) {
    if (ripple && !stepRipple(*ripple)) {
      ripple.reset();
    }
  }
  if (attractor_ && !slots_.at(*attractor_)) {
    attractor_.reset();
  }
}

const Ripple* RipplePool::attractor() const {
  if (!attractor_ || !slots_.at(*attractor_)) {
    return nullptr;
  }
  return &*slots_.at(*attractor_);
}

bool stepRipple(Ripple& ripple) {
  switch (ripple.kind) {
  case 0:
    ripple.sprite = stepOf(SplashSteps, ripple.frame / 2);
    break;
  case -1:
    ripple.sprite = stepOf(SinkSteps, ripple.frame);
    break;
  case -2:
    ripple.sprite = stepOf(SmallNibbleSteps, ripple.frame / 3);
    break;
  case -3:
    ripple.sprite = stepOf(NibbleSteps, ripple.frame);
    break;
  case -4:
    ripple.sprite = stepOf(BiteSteps, ripple.frame);
    break;
  default:
    return false;
  }
  ++ripple.frame;
  return ripple.sprite != -1 && ripple.sprite < StepCount;
}

FishSprite markerSprite(const FishMarker& marker) {
  return spriteOf(marker.mayBite ? MayBiteSprite : NoBiteSprite);
}

std::optional<FishSprite> rippleSprite(const Ripple& ripple) {
  if (ripple.sprite <= 0) {
    return std::nullopt;
  }
  const std::int32_t index =
      ripple.kind == 0 ? ripple.sprite - 1 : ripple.sprite + RingSpriteOffset;
  if (index < 0 || std::cmp_greater_equal(index, RippleSprites.size())) {
    return std::nullopt;
  }
  return spriteOf(RippleSprites.at(static_cast<std::size_t>(index)));
}

float rippleUnitsPerTexel(std::int32_t mapScale, std::int32_t rippleScale) {
  // (mapScale * scale) >> 12, then each corner's (offset * that) >> 12.
  const std::int64_t perAxis = (static_cast<std::int64_t>(mapScale) * rippleScale) >> 12;
  return static_cast<float>(perAxis) / FixedOne;
}

} // namespace dwe::game::fishing
