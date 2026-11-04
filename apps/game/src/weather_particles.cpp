#include "weather_particles.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dwe::app {

namespace {

using game::weather::WeatherLook;

// The original's screen width: particle counts are per this many pixels of width.
constexpr float OriginalWidth = 320.0F;
// Particles wrap around a field this much wider than the area on every side, so they appear and
// vanish off screen.
constexpr float Margin = 24.0F;

// Rain, in original pixels and seconds.
constexpr float RainSpeed = 470.0F;
// How far right a drop moves for each pixel it falls.
constexpr float RainSlant = 0.22F;
constexpr float RainShortest = 9.0F;
constexpr float RainLongest = 18.0F;
constexpr float SplashSeconds = 0.22F;
constexpr float SplashRadius = 5.0F;
// How flat a splash ring is on the ground.
constexpr float SplashFlatness = 0.4F;

// Snow.
constexpr float SnowWind = 7.0F;
constexpr float SnowSmallest = 0.7F;
constexpr float SnowLargest = 1.9F;

// Petals and leaves: sizes (half the length) in original pixels, falls in pixels a second.
constexpr float PetalSmallest = 2.0F;
constexpr float PetalLargest = 3.0F;
constexpr float LeafSmallest = 3.0F;
constexpr float LeafLargest = 4.4F;
constexpr float FallingWind = 11.0F;
// Under full rain or snow only this part of them falls.
constexpr float FallingInWeather = 0.4F;
constexpr std::array PetalColours{glm::vec3{1.0F, 0.72F, 0.84F}, glm::vec3{1.0F, 0.84F, 0.91F},
                                  glm::vec3{0.95F, 0.58F, 0.76F}};
constexpr std::array LeafColours{glm::vec3{0.90F, 0.48F, 0.12F}, glm::vec3{0.74F, 0.25F, 0.10F},
                                 glm::vec3{0.93F, 0.71F, 0.20F}, glm::vec3{0.56F, 0.33F, 0.14F}};

// Clouds: one tile of the noise covers this many background pixels, and they drift this fast.
constexpr float CloudTile = 420.0F;
constexpr glm::vec2 CloudWind{9.0F, 3.0F};

// Colours by day and by night.
constexpr glm::vec3 OvercastDay{0.46F, 0.49F, 0.55F};
constexpr glm::vec3 OvercastNight{0.07F, 0.09F, 0.13F};
constexpr glm::vec3 CloudShadow{0.04F, 0.06F, 0.10F};
constexpr glm::vec3 SnowHaze{0.86F, 0.90F, 0.97F};
constexpr glm::vec3 Sunlight{1.0F, 0.80F, 0.52F};
constexpr glm::vec4 RainDay{0.84F, 0.88F, 0.96F, 0.65F};
constexpr glm::vec4 RainNight{0.55F, 0.60F, 0.72F, 0.38F};
constexpr glm::vec4 SnowDay{0.97F, 0.98F, 1.0F, 0.85F};
constexpr glm::vec4 SnowNight{0.72F, 0.76F, 0.86F, 0.75F};

std::uint64_t mix(std::uint64_t value) {
  value += 0x9E3779B97F4A7C15ULL;
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

// A number from 0 to 1 for a particle, a cycle of it and one of its properties.
float unit(std::uint64_t particle, std::uint64_t cycle, std::uint64_t property) {
  const std::uint64_t value = mix(mix(mix(particle) ^ cycle) ^ (property * 0x100000001B3ULL));
  return static_cast<float>(value >> 40U) / static_cast<float>(1U << 24U);
}

float wrap(float value, float span) {
  const float wrapped = std::fmod(value, span);
  return wrapped < 0.0F ? wrapped + span : wrapped;
}

// The number of particles for a strength, and how visible the last one is, so a fade adds and
// removes them one by one without popping.
struct Count {
  std::size_t whole = 0;
  float last = 0.0F;
};

Count countOf(float perScreen, float width, float strength) {
  const float count = std::max(perScreen * (width / OriginalWidth) * strength, 0.0F);
  const float whole = std::floor(count);
  return {.whole = static_cast<std::size_t>(whole) + (count > whole ? 1U : 0U),
          .last = count > whole ? count - whole : 1.0F};
}

class Builder {
public:
  explicit Builder(const WeatherFrame& frame) : frame_(&frame) {}

  [[nodiscard]] float width() const { return frame_->areaSize.x / frame_->scale; }
  [[nodiscard]] float height() const { return frame_->areaSize.y / frame_->scale; }

  // A point of the area in original pixels, in window pixels.
  [[nodiscard]] glm::vec2 window(glm::vec2 point) const {
    return frame_->areaOrigin + (point * frame_->scale);
  }

  void solid(glm::vec3 color, float alpha, WeatherBlendMode blend) {
    if (alpha <= 0.0F) {
      return;
    }
    WeatherQuad quad{.color = glm::vec4{color, alpha}, .blend = blend};
    quad.corners = {frame_->areaOrigin, frame_->areaOrigin + glm::vec2{frame_->areaSize.x, 0.0F},
                    frame_->areaOrigin + glm::vec2{0.0F, frame_->areaSize.y},
                    frame_->areaOrigin + frame_->areaSize};
    quads.push_back(quad);
  }

  // The sky's veil: a flat tint (colour and alpha) with the cloud shadows (strength) over it.
  void sky(glm::vec3 tint, float tintAlpha, float shadow) {
    if (tintAlpha <= 0.0F && shadow <= 0.0F) {
      return;
    }
    WeatherQuad quad{.color = glm::vec4{tint, std::max(tintAlpha, 0.0F)},
                     .shadow = glm::vec4{CloudShadow, std::max(shadow, 0.0F)},
                     .shape = WeatherShape::Clouds};
    quad.corners = {frame_->areaOrigin, frame_->areaOrigin + glm::vec2{frame_->areaSize.x, 0.0F},
                    frame_->areaOrigin + glm::vec2{0.0F, frame_->areaSize.y},
                    frame_->areaOrigin + frame_->areaSize};
    const glm::vec2 from =
        frame_->scroll + (CloudWind * static_cast<float>(std::fmod(frame_->seconds, 3600.0)));
    const glm::vec2 size{width(), height()};
    quad.uv = {from / CloudTile, (from + glm::vec2{size.x, 0.0F}) / CloudTile,
               (from + glm::vec2{0.0F, size.y}) / CloudTile, (from + size) / CloudTile};
    quads.push_back(quad);
  }

  // A quad around a centre in original pixels, half sizes `half`.
  void blob(glm::vec2 centre, glm::vec2 half, glm::vec4 color, WeatherShape shape) {
    WeatherQuad quad{.color = color, .shape = shape};
    quad.corners = {window(centre - half), window(centre + glm::vec2{half.x, -half.y}),
                    window(centre + glm::vec2{-half.x, half.y}), window(centre + half)};
    quads.push_back(quad);
  }

  // A quad around a centre in original pixels, half sizes `half` along and across, turned by
  // `angle` radians.
  void turned(glm::vec2 centre, glm::vec2 half, float angle, glm::vec4 color, WeatherShape shape) {
    const glm::vec2 along = glm::vec2{std::cos(angle), std::sin(angle)} * half.x;
    const glm::vec2 across = glm::vec2{-std::sin(angle), std::cos(angle)} * half.y;
    WeatherQuad quad{.color = color, .shape = shape};
    quad.corners = {window(centre - along - across), window(centre + along - across),
                    window(centre - along + across), window(centre + along + across)};
    quads.push_back(quad);
  }

  // A streak from its tail to its head, in original pixels, one window pixel wide at least.
  void streak(glm::vec2 tail, glm::vec2 head, glm::vec4 color) {
    const glm::vec2 from = window(tail);
    const glm::vec2 to = window(head);
    const glm::vec2 along = to - from;
    const float length = glm::length(along);
    if (length <= 0.0F) {
      return;
    }
    const float half = std::max(frame_->scale * 0.5F, 0.75F);
    const glm::vec2 across = glm::vec2{-along.y, along.x} / length * half;
    WeatherQuad quad{.color = color, .shape = WeatherShape::Streak};
    quad.corners = {from - across, from + across, to - across, to + across};
    quads.push_back(quad);
  }

  std::vector<WeatherQuad> quads;

private:
  const WeatherFrame* frame_;
};

void addRain(Builder& out, const WeatherLook& look, const WeatherFrame& frame) {
  const float width = out.width();
  const float height = out.height();
  const Count count = countOf(HeavyRainDrops, width, look.rain);
  const glm::vec4 color = glm::mix(RainNight, RainDay, frame.daylight);
  const float length = RainShortest + ((RainLongest - RainShortest) * std::min(look.rain, 1.0F));
  const glm::vec2 fall = glm::normalize(glm::vec2{RainSlant, 1.0F});
  const auto seconds = static_cast<float>(std::fmod(frame.seconds, 86400.0));
  for (std::size_t i = 0; i < count.whole; ++i) {
    const float shown = i + 1 == count.whole ? count.last : 1.0F;
    // Drops fall from different heights: rain is all around, not only from the top of the screen.
    const float distance = height * (0.45F + (0.7F * unit(i, 0, 1)));
    const float fallSeconds = distance / RainSpeed;
    const float cycleSeconds = fallSeconds + SplashSeconds;
    const float time = seconds + (unit(i, 0, 2) * cycleSeconds);
    const auto cycle = static_cast<std::uint64_t>(std::floor(time / cycleSeconds));
    const float local = time - (static_cast<float>(cycle) * cycleSeconds);
    // Where it lands on the ground, which moves with the camera.
    const glm::vec2 land{wrap((unit(i, cycle, 3) * (width + (2.0F * Margin))) - frame.scroll.x,
                              width + (2.0F * Margin)) -
                             Margin,
                         wrap((unit(i, cycle, 4) * (height + (2.0F * Margin))) - frame.scroll.y,
                              height + (2.0F * Margin)) -
                             Margin};
    if (local < fallSeconds) {
      const float left = (fallSeconds - local) * RainSpeed;
      const glm::vec2 head = land - (glm::vec2{RainSlant, 1.0F} * left);
      out.streak(head - (fall * length), head, {glm::vec3{color}, color.a * shown});
    } else {
      const float age = (local - fallSeconds) / SplashSeconds;
      const float radius = SplashRadius * (0.35F + (0.65F * age));
      out.blob(land, {radius, radius * SplashFlatness},
               {glm::vec3{color}, color.a * 0.8F * (1.0F - age) * shown}, WeatherShape::Ring);
    }
  }
}

void addSnow(Builder& out, const WeatherLook& look, const WeatherFrame& frame) {
  const float width = out.width() + (2.0F * Margin);
  const float height = out.height() + (2.0F * Margin);
  const Count count = countOf(HeavySnowFlakes, out.width(), look.snow);
  const glm::vec4 color = glm::mix(SnowNight, SnowDay, frame.daylight);
  const auto seconds = static_cast<float>(std::fmod(frame.seconds, 86400.0));
  constexpr float Tau = 2.0F * std::numbers::pi_v<float>;
  for (std::size_t i = 0; i < count.whole; ++i) {
    const float shown = i + 1 == count.whole ? count.last : 1.0F;
    const float size = SnowSmallest + ((SnowLargest - SnowSmallest) * unit(i, 0, 1));
    // Big flakes are near: they fall and drift faster and move more with the camera.
    const float depth = 0.55F + (0.6F * (size - SnowSmallest) / (SnowLargest - SnowSmallest));
    const float speed = (13.0F + (17.0F * unit(i, 0, 2))) * depth;
    const float sway = 3.0F + (9.0F * unit(i, 0, 3));
    const float swayRate = 0.2F + (0.45F * unit(i, 0, 4));
    const float phase = unit(i, 0, 5) * Tau;
    const glm::vec2 at{
        wrap((unit(i, 0, 6) * width) + (SnowWind * depth * seconds) +
                 (sway * std::sin((Tau * swayRate * seconds) + phase)) - (frame.scroll.x * depth),
             width) -
            Margin,
        wrap((unit(i, 0, 7) * height) + (speed * seconds) - (frame.scroll.y * depth), height) -
            Margin};
    out.blob(at, glm::vec2{size * 1.6F}, {glm::vec3{color}, color.a * shown}, WeatherShape::Dot);
  }
}

// Petals or leaves: `amount` of the most, falling slowly with the wind, swaying and tumbling.
template <std::size_t Colours>
void addFalling(Builder& out, float amount, float perScreen, bool leaves,
                const std::array<glm::vec3, Colours>& colours, const WeatherFrame& frame) {
  const float width = out.width() + (2.0F * Margin);
  const float height = out.height() + (2.0F * Margin);
  const Count count = countOf(perScreen, out.width(), amount);
  const auto seconds = static_cast<float>(std::fmod(frame.seconds, 86400.0));
  const float light = 0.45F + (0.55F * frame.daylight);
  const float smallest = leaves ? LeafSmallest : PetalSmallest;
  const float largest = leaves ? LeafLargest : PetalLargest;
  // Petals and leaves of their own numbers, so turning one season into the other never moves the
  // snow's flakes.
  const std::uint64_t kind = leaves ? 0x1EAF0000ULL : 0x9E7A1000ULL;
  constexpr float Tau = 2.0F * std::numbers::pi_v<float>;
  for (std::size_t n = 0; n < count.whole; ++n) {
    const std::uint64_t i = kind + n;
    const float shown = n + 1 == count.whole ? count.last : 1.0F;
    const float size = smallest + ((largest - smallest) * unit(i, 0, 1));
    const float depth = 0.6F + (0.5F * (size - smallest) / (largest - smallest));
    const float speed =
        (leaves ? 15.0F + (12.0F * unit(i, 0, 2)) : 9.0F + (8.0F * unit(i, 0, 2))) * depth;
    const float sway = (leaves ? 8.0F : 12.0F) + (10.0F * unit(i, 0, 3));
    const float swayRate = 0.18F + (0.3F * unit(i, 0, 4));
    const float phase = unit(i, 0, 5) * Tau;
    const glm::vec2 at{
        wrap((unit(i, 0, 6) * width) + (FallingWind * depth * seconds) +
                 (sway * std::sin((Tau * swayRate * seconds) + phase)) - (frame.scroll.x * depth),
             width) -
            Margin,
        wrap((unit(i, 0, 7) * height) + (speed * seconds) - (frame.scroll.y * depth), height) -
            Margin};
    // It turns as it sways and flips over (its width shrinks as it shows its edge).
    const float spin = (unit(i, 0, 8) - 0.5F) * 3.0F;
    const float angle =
        phase + (spin * seconds) + (0.6F * std::sin((Tau * swayRate * seconds) + phase + 1.0F));
    const float flip =
        0.3F +
        (0.7F * std::abs(std::cos((Tau * (0.3F + (0.5F * unit(i, 0, 9))) * seconds) + phase)));
    const glm::vec3 colour =
        colours.at(static_cast<std::size_t>(unit(i, 0, 10) * static_cast<float>(Colours)) %
                   Colours) *
        light;
    out.turned(at, {size, size * (leaves ? 0.55F : 0.7F) * flip}, angle,
               {colour, (leaves ? 0.95F : 0.9F) * shown},
               leaves ? WeatherShape::Leaf : WeatherShape::Petal);
  }
}

} // namespace

std::vector<WeatherQuad> seasonQuads(const game::weather::SeasonLook& season,
                                     const WeatherLook& weather, const WeatherFrame& frame) {
  Builder out{frame};
  if ((season.petals <= 0.0F && season.leaves <= 0.0F) || frame.scale <= 0.0F ||
      frame.areaSize.x <= 0.0F || frame.areaSize.y <= 0.0F) {
    return {};
  }
  WeatherFrame lit = frame;
  lit.daylight = std::clamp(frame.daylight, 0.0F, 1.0F);
  const float falling = 1.0F - ((1.0F - FallingInWeather) *
                                std::clamp(std::max(weather.rain, weather.snow), 0.0F, 1.0F));
  if (season.petals > 0.0F) {
    addFalling(out, season.petals * falling, SeasonPetals, false, PetalColours, lit);
  }
  if (season.leaves > 0.0F) {
    addFalling(out, season.leaves * falling, SeasonLeaves, true, LeafColours, lit);
  }
  return std::move(out.quads);
}

std::vector<WeatherQuad> weatherQuads(const WeatherLook& look, const WeatherFrame& frame) {
  Builder out{frame};
  if (look.clear() || frame.scale <= 0.0F || frame.areaSize.x <= 0.0F || frame.areaSize.y <= 0.0F) {
    return {};
  }
  const float daylight = std::clamp(frame.daylight, 0.0F, 1.0F);
  // Overcast: less light (a dark veil, deeper in the rain) and a grey one that dulls the colours,
  // as one blend: dst (1 - dark) (1 - grey) + colour grey. Snow adds a pale haze over it.
  const float dark = ((look.cloudCover * 0.16F) + (look.rain * 0.12F)) * (0.5F + (0.5F * daylight));
  const float grey = look.cloudCover * 0.16F;
  const float veil = 1.0F - ((1.0F - dark) * (1.0F - grey));
  const float haze = look.snow * 0.08F * (0.4F + (0.6F * daylight));
  const float tint = 1.0F - ((1.0F - veil) * (1.0F - haze));
  const glm::vec3 tinted =
      (glm::mix(OvercastNight, OvercastDay, daylight) * grey * (1.0F - haze)) + (SnowHaze * haze);
  // The clouds' shadows pass over the ground while the sun is up.
  const float shadow =
      look.cloudCover * 0.3F * (0.25F + (0.75F * daylight)) * (1.0F - (0.5F * look.rain));
  out.sky(tint > 0.0F ? tinted / tint : glm::vec3{0.0F}, tint, shadow);
  // A clear day is warmer and a little brighter.
  out.solid(Sunlight, look.sunshine * 0.09F * daylight, WeatherBlendMode::Additive);
  WeatherFrame lit = frame;
  lit.daylight = daylight;
  if (look.rain > 0.0F) {
    addRain(out, look, lit);
  }
  if (look.snow > 0.0F) {
    addSnow(out, look, lit);
  }
  return std::move(out.quads);
}

std::vector<std::byte> cloudNoisePixels(std::uint32_t side) {
  // Value noise on lattices of 4, 8 and 16 cells, each wrapping at the picture's edge.
  const auto lattice = [](std::uint32_t cells, std::uint32_t x, std::uint32_t y) {
    return unit((static_cast<std::uint64_t>(cells) << 32U) | ((x % cells) << 16U) | (y % cells), 0,
                9);
  };
  const auto smooth = [](float t) { return t * t * (3.0F - (2.0F * t)); };
  std::vector<std::byte> pixels(static_cast<std::size_t>(side) * side * 4U);
  for (std::uint32_t y = 0; y < side; ++y) {
    for (std::uint32_t x = 0; x < side; ++x) {
      float value = 0.0F;
      float weight = 0.0F;
      float amplitude = 1.0F;
      for (const std::uint32_t cells : {4U, 8U, 16U}) {
        const float fx =
            static_cast<float>(x) * static_cast<float>(cells) / static_cast<float>(side);
        const float fy =
            static_cast<float>(y) * static_cast<float>(cells) / static_cast<float>(side);
        const auto cx = static_cast<std::uint32_t>(fx);
        const auto cy = static_cast<std::uint32_t>(fy);
        const float tx = smooth(fx - static_cast<float>(cx));
        const float ty = smooth(fy - static_cast<float>(cy));
        const float top = glm::mix(lattice(cells, cx, cy), lattice(cells, cx + 1, cy), tx);
        const float bottom =
            glm::mix(lattice(cells, cx, cy + 1), lattice(cells, cx + 1, cy + 1), tx);
        value += glm::mix(top, bottom, ty) * amplitude;
        weight += amplitude;
        amplitude *= 0.5F;
      }
      const auto level = static_cast<std::byte>(std::clamp(value / weight, 0.0F, 1.0F) * 255.0F);
      const std::size_t at = (static_cast<std::size_t>(y) * side + x) * 4U;
      pixels[at] = level;
      pixels[at + 1] = level;
      pixels[at + 2] = level;
      pixels[at + 3] = std::byte{255};
    }
  }
  return pixels;
}

} // namespace dwe::app
