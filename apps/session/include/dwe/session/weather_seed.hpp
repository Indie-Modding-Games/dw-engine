#pragma once

#include <dwe/session/session_system.hpp>

#include <cstdint>
#include <memory>
#include <string_view>

namespace dwe::session {

// The playthrough's weather seed (game::weather: an engine improvement, the original has no
// weather). The field's weather is a pure function of it, the area and the game time, so keeping it
// in the save is all it takes for the weather to come back the same after a load. A new game takes
// it from the game's seed (game::weather::weatherSeedOf: a mix, never a roll of the game's random
// numbers); a save from before the weather has no `weather` section and takes it the same way from
// the session's seed, then keeps it in its next save.
class WeatherSeed final : public SessionSystem {
public:
  // The save section's name.
  static constexpr std::string_view SaveName = "weather";

  WeatherSeed();
  ~WeatherSeed() override;
  WeatherSeed(const WeatherSeed&) = delete;
  WeatherSeed& operator=(const WeatherSeed&) = delete;
  WeatherSeed(WeatherSeed&&) = delete;
  WeatherSeed& operator=(WeatherSeed&&) = delete;

  core::Result<void> addSaveSections(game::save::SaveSections& sections) override;
  core::Result<void> startNewGame(GameSession& session) override;
  void afterLoad(GameSession& session) override;

  [[nodiscard]] std::uint64_t seed() const { return seed_; }
  void setSeed(std::uint64_t seed) {
    seed_ = seed;
    missing_ = false;
  }

private:
  class Serializer;

  std::uint64_t seed_ = 0;
  // The save read had no weather section.
  bool missing_ = false;
  std::unique_ptr<Serializer> serializer_;
};

} // namespace dwe::session
