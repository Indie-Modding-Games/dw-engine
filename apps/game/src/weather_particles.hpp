#pragma once

#include <dwe/game/weather/look.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dwe::app {

// How a weather quad is shaded (weather.slang).
enum class WeatherShape : std::uint32_t {
  // The colour as it is.
  Solid = 0,
  // A rain streak: v runs from its tail (0, clear) to its head (1).
  Streak = 1,
  // A soft round snowflake.
  Dot = 2,
  // The sky's veil: a flat tint (the colour) under the cloud shadows (`shadow`, cut out of the
  // noise
  // picture into soft patches), in one pass over the map.
  Clouds = 3,
  // A splash ring.
  Ring = 4,
  // A falling leaf: pointed at both ends of u, a darker rib along it.
  Leaf = 5,
  // A falling petal: a soft oval, lighter in the middle.
  Petal = 6,
};

enum class WeatherBlendMode : std::uint8_t { Alpha, Additive };

// One quad of the weather in the window's pixels (x right, y down), corners 0 1 2 and 2 1 3.
struct WeatherQuad {
  std::array<glm::vec2, 4> corners{};
  std::array<glm::vec2, 4> uv{glm::vec2{0.0F, 0.0F}, glm::vec2{1.0F, 0.0F}, glm::vec2{0.0F, 1.0F},
                              glm::vec2{1.0F, 1.0F}};
  glm::vec4 color{1.0F};
  // Clouds only: the shadows' colour and strength over the tint.
  glm::vec4 shadow{0.0F};
  WeatherShape shape = WeatherShape::Solid;
  WeatherBlendMode blend = WeatherBlendMode::Alpha;
};

// Where and when the weather is drawn this frame.
struct WeatherFrame {
  // The part of the window the map covers, in window pixels (MapCamera::backgroundRect): the
  // weather never draws on the bars of a wider window.
  glm::vec2 areaOrigin{0.0F};
  glm::vec2 areaSize{320.0F, 240.0F};
  // Window pixels per original screen pixel (MapCamera::scale): particles are sized and move in the
  // original's pixels, so they look the same at any window size.
  float scale = 1.0F;
  // The camera's scroll over the background, in background pixels: rain lands and clouds pass on
  // the ground, so they move with it as the camera pans.
  glm::vec2 scroll{0.0F};
  // Seconds of weather animation (it stops while the field is paused).
  double seconds = 0.0;
  // 1 by day, 0.5 at dusk, 0 at night: the sun's warmth and the cloud shadows go with the daylight,
  // and rain and snow are dimmer at night.
  float daylight = 1.0F;
};

// The quads of a weather look, back to front: the sky's veil (overcast tint, snow haze and cloud
// shadows in one quad), the sun's warmth, then the rain's streaks and splashes or the snowflakes. A
// pure function of the look and the frame: the particles are placed by hashing their number and
// cycle, never by a random stream.
[[nodiscard]] std::vector<WeatherQuad> weatherQuads(const game::weather::WeatherLook& look,
                                                    const WeatherFrame& frame);

// The season's falling petals (spring) or leaves (autumn) over the map, as rotated quads that
// tumble and sway down: few and light, fewer under rain or snow, dimmer at night. A pure function
// of the looks and the frame, as weatherQuads; nothing when the season shows none.
[[nodiscard]] std::vector<WeatherQuad> seasonQuads(const game::weather::SeasonLook& season,
                                                   const game::weather::WeatherLook& weather,
                                                   const WeatherFrame& frame);

// The cloud shadows' picture: a tileable value noise, `side` texels square, as RGBA (the noise in
// every channel).
[[nodiscard]] std::vector<std::byte> cloudNoisePixels(std::uint32_t side);

// How many drops and flakes full rain and snow draw over the original's 320-pixel-wide screen; a
// wider window draws more, so the density stays the same.
inline constexpr float HeavyRainDrops = 240.0F;
inline constexpr float HeavySnowFlakes = 200.0F;
// How many petals and leaves a full season draws over the original's screen width: light.
inline constexpr float SeasonPetals = 18.0F;
inline constexpr float SeasonLeaves = 14.0F;

} // namespace dwe::app
