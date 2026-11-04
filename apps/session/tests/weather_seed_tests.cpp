#include <dwe/game/save/save_file.hpp>
#include <dwe/game/weather/climate.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/weather_seed.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "session_fixture.hpp"

using dwe::session::WeatherSeed;
using dwe::session::testing::careFor;
using dwe::session::testing::errorOf;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

constexpr double SixtyGameHoursPerSecond = 3600.0;
constexpr double GameDaySeconds = 24.0 * 60.0 * 60.0 / SixtyGameHoursPerSecond;

struct Played {
  std::unique_ptr<dwe::session::GameSession> session;
  WeatherSeed* weather = nullptr;
};

Played play(const TestPacks& packs, bool withWeather, std::uint64_t seed) {
  std::vector<std::unique_ptr<dwe::session::SessionSystem>> systems;
  WeatherSeed* weather = nullptr;
  if (withWeather) {
    auto made = std::make_unique<WeatherSeed>();
    weather = made.get();
    systems.push_back(std::move(made));
  }
  return {.session = newGameWith(packs, std::move(systems), nullptr, seed), .weather = weather};
}

// A save through its text, as a file on disk would be.
dwe::game::save::SaveFile throughText(const dwe::game::save::SaveFile& file) {
  const auto text = dwe::game::save::writeSaveFile(file);
  REQUIRE_MESSAGE(text.has_value(), errorOf(text));
  auto read = dwe::game::save::parseSaveFile(*text);
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  return std::move(*read);
}

} // namespace

TEST_CASE("the weather seed never rolls the game's random numbers") {
  const TestPacks packs;
  Played with = play(packs, true, 11);
  Played without = play(packs, false, 11);
  CHECK(with.weather->seed() == dwe::game::weather::weatherSeedOf(11));
  for (auto* session : {with.session.get(), without.session.get()}) {
    session->clock().setSpeed(SixtyGameHoursPerSecond);
    careFor(*session, 2 * GameDaySeconds);
  }
  // The same rolls in the same order with the weather as without it.
  CHECK(with.session->random().state() == without.session->random().state());
  CHECK(with.session->partner().randomState() == without.session->partner().randomState());
  CHECK(with.session->partner().state() == without.session->partner().state());
}

TEST_CASE("a save keeps the weather seed; a save from before the weather takes one") {
  const TestPacks packs;
  Played saved = play(packs, true, 11);
  saved.weather->setSeed(123456789012345ULL);
  const auto file = saved.session->saveFile();
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));
  REQUIRE(file->sections.contains(WeatherSeed::SaveName));

  // Another seed: the loaded game's weather comes from the save.
  Played loaded = play(packs, true, 99);
  const auto read = loaded.session->loadFile(throughText(*file));
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  CHECK(loaded.weather->seed() == 123456789012345ULL);

  // A save written before the weather existed has no section: it loads and takes the session's.
  Played old = play(packs, false, 5);
  const auto oldFile = old.session->saveFile();
  REQUIRE_MESSAGE(oldFile.has_value(), errorOf(oldFile));
  CHECK_FALSE(oldFile->sections.contains(WeatherSeed::SaveName));
  Played upgraded = play(packs, true, 99);
  upgraded.weather->setSeed(1);
  const auto readOld = upgraded.session->loadFile(throughText(*oldFile));
  REQUIRE_MESSAGE(readOld.has_value(), errorOf(readOld));
  CHECK(upgraded.weather->seed() == dwe::game::weather::weatherSeedOf(99));
  // Its next save keeps that seed.
  const auto resaved = upgraded.session->saveFile();
  REQUIRE_MESSAGE(resaved.has_value(), errorOf(resaved));
  Played again = play(packs, true, 3);
  REQUIRE(again.session->loadFile(throughText(*resaved)).has_value());
  CHECK(again.weather->seed() == dwe::game::weather::weatherSeedOf(99));
}

TEST_CASE("a weather section that is not a seed is refused") {
  WeatherSeed weather;
  dwe::game::save::SaveSections sections;
  REQUIRE(weather.addSaveSections(sections).has_value());
  dwe::game::save::SaveFile file;
  dwe::core::YamlNode state = dwe::core::YamlNode::mapping();
  state.set("seed", dwe::core::YamlNode::scalar("cloudy"));
  file.sections.emplace(std::string{WeatherSeed::SaveName},
                        dwe::game::save::SavedSection{.version = 1, .state = state});
  CHECK_FALSE(sections.read(file).has_value());
}
