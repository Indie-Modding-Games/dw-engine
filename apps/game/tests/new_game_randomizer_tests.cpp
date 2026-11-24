#include <dwe/game/randomizer/run_settings.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "new_game_randomizer.hpp"

using dwe::app::NewGameRandomizer;
using dwe::game::randomizer::chaos;
using dwe::game::randomizer::LearnChance;
using dwe::game::randomizer::runCode;
using dwe::game::randomizer::RunSettings;
using dwe::game::randomizer::seedText;

namespace {

constexpr std::uint64_t Seed = 0x1234567890U;
using Outcome = NewGameRandomizer::Outcome;

template <typename T>
T settingOf(const NewGameRandomizer& setup, std::string_view name) {
  for (const auto& setting : setup.settings()) {
    if (setting.name == "new_game_randomizer." + std::string{name}) {
      const T* value = std::get_if<T>(&setting.value);
      REQUIRE(value != nullptr);
      return *value;
    }
  }
  FAIL("no setting " << name);
  return T{};
}

} // namespace

TEST_CASE("the randomizer's setup starts off with the seed it is given") {
  NewGameRandomizer setup;
  setup.open(Seed);
  CHECK(setup.run() == RunSettings{.seed = Seed});
  CHECK_FALSE(setup.run().active());
  CHECK(settingOf<std::string>(setup, "seed") == seedText(Seed));
  CHECK(settingOf<std::string>(setup, "learn") == "original");
  CHECK_FALSE(settingOf<bool>(setup, "active"));
}

TEST_CASE("the setup toggles each option, steps the learning chance and applies the presets") {
  NewGameRandomizer setup;
  setup.open(Seed);
  CHECK(setup.act("toggle:enemies") == Outcome::Changed);
  CHECK(setup.run().enemies);
  CHECK(setup.act("learn") == Outcome::Changed);
  CHECK(setup.run().learnChance == LearnChance::Quarter);
  for (int step = 0; step < 4; ++step) {
    static_cast<void>(setup.act("learn"));
  }
  CHECK(setup.run().learnChance == LearnChance::Original);
  static_cast<void>(setup.act("preset:chaos"));
  CHECK(setup.run() == chaos(Seed));
  static_cast<void>(setup.act("preset:off"));
  CHECK(setup.run() == RunSettings{.seed = Seed});
  static_cast<void>(setup.act("reseed"));
  CHECK(setup.run().seed != Seed);
  CHECK(setup.act("start") == Outcome::Start);
  CHECK(setup.act("back") == Outcome::Back);
}

TEST_CASE("a typed seed or run code replaces the seed; anything else leaves it and says so") {
  NewGameRandomizer setup;
  setup.open(Seed);
  static_cast<void>(setup.act("edit"));
  REQUIRE(setup.takesText());
  setup.type("k7qh-2m9x!", false);
  CHECK(settingOf<std::string>(setup, "draft") == "K7QH-2M9X");
  setup.type("", true);
  CHECK_FALSE(setup.takesText());
  CHECK(settingOf<std::string>(setup, "seed") == "K7QH2M9X");

  RunSettings shared = chaos(Seed);
  shared.drops = false;
  static_cast<void>(setup.act("edit"));
  setup.type(runCode(shared), true);
  CHECK(setup.run() == shared);

  static_cast<void>(setup.act("edit"));
  setup.type("12", true);
  CHECK(settingOf<bool>(setup, "invalid"));
  CHECK(setup.run() == shared);

  static_cast<void>(setup.act("edit"));
  setup.type("1", false);
  static_cast<void>(setup.act("erase"));
  static_cast<void>(setup.act("erase"));
  CHECK_FALSE(setup.takesText());
  CHECK(setup.run() == shared);
}

TEST_CASE("paste reads the clipboard as a typed code") {
  NewGameRandomizer setup;
  RunSettings shared = chaos(Seed);
  shared.learnChance = LearnChance::Half;
  setup.setClipboard([&shared] { return " " + runCode(shared) + "\n"; });
  setup.open(1);
  static_cast<void>(setup.act("paste"));
  CHECK_FALSE(setup.takesText());
  CHECK(setup.run() == shared);
}
