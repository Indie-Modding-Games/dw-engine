#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/audio/audio.hpp>
#include <dwe/audio/clip.hpp>
#include <dwe/game/weather/look.hpp>

#include <cstdint>
#include <memory>
#include <optional>

namespace dwe::app {

// The weather's sound (an engine improvement): a rain loop whose volume follows the rain, wind
// gusts now and then while it snows or the wind blows, and a storm's thunder a moment after each
// lightning flash. The game has no recording of any of them; the nearest sounds of its own banks
// stand in (a steady hiss of the factory's bank, looped with a crossfade, a gust of the frozen
// area's bank and a long rumble of the ancient area's bank). Without them, or without audio, it
// stays silent. The voices cannot change their volume while they play, so the rain's loop starts
// again at the new volume when the rain moves a step. Gusts are timed by a counter of its own and
// thunder by the strikes the flashes come from (lightningStrike), never the game's random numbers.
class WeatherAmbience {
public:
  // `audio` may be null (no sound); both must outlive the ambience.
  WeatherAmbience(audio::Audio* audio, const assets::VirtualFileSystem& files);
  ~WeatherAmbience();
  WeatherAmbience(const WeatherAmbience&) = delete;
  WeatherAmbience& operator=(const WeatherAmbience&) = delete;
  WeatherAmbience(WeatherAmbience&&) = delete;
  WeatherAmbience& operator=(WeatherAmbience&&) = delete;

  // One frame: the look shown, the seconds since the last and the seconds of weather animation
  // (FieldScene::weatherSeconds, the clock the lightning flashes by).
  void update(const game::weather::WeatherLook& look, double seconds, double weatherSeconds);
  // Stops every voice.
  void stop();

  // The rain loop's volume for a rain strength: in steps of RainStep, 0 below the first.
  [[nodiscard]] static float rainGain(float rain);
  static constexpr float RainStep = 0.125F;

private:
  void loadClips();

  audio::Audio* audio_;
  const assets::VirtualFileSystem* files_;
  bool loaded_ = false;
  std::shared_ptr<const audio::Clip> rain_;
  std::shared_ptr<const audio::Clip> gust_;
  std::shared_ptr<const audio::Clip> thunder_;
  std::optional<audio::VoiceId> rainVoice_;
  float rainPlaying_ = 0.0F;
  double gustSeconds_ = 2.0;
  std::uint64_t gusts_ = 0;
  // The weather seconds of the last frame: a thunder sounds once its time is passed.
  std::optional<double> heardUntil_;
};

} // namespace dwe::app
