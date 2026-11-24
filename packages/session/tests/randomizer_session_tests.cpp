#include <dwe/game/randomizer/run_settings.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/slot_names.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <string>

#include "session_fixture.hpp"

using dwe::game::randomizer::chaos;
using dwe::game::randomizer::LearnChance;
using dwe::game::randomizer::runCode;
using dwe::game::randomizer::RunSettings;
using dwe::session::GameSession;
using dwe::session::summaryFromSections;
using dwe::session::testing::careFor;
using dwe::session::testing::errorOf;
using dwe::session::testing::madeUpTable;
using dwe::session::testing::SixtyGameHoursPerSecond;
using dwe::session::testing::TestPacks;

namespace {

constexpr std::uint64_t GameSeed = 11;
constexpr std::uint64_t RunSeed = 0xABCDE12345U;
constexpr double OneGameDay = 24.0;

std::unique_ptr<GameSession> randomizedGame(const TestPacks& packs, const RunSettings& run) {
  auto session = GameSession::create({.files = &packs.files(),
                                      .rulesetId = "original",
                                      .rules = std::nullopt,
                                      .randomizer = run,
                                      .species = madeUpTable(),
                                      .seed = GameSeed,
                                      .storage = nullptr,
                                      .gameVersion = "test",
                                      .wallClock = [] { return dwe::session::testing::SaveDate; },
                                      .systems = {}});
  REQUIRE_MESSAGE(session.has_value(), errorOf(session));
  auto started = (*session)->startNewGame();
  REQUIRE_MESSAGE(started.has_value(), errorOf(started));
  return std::move(*session);
}

} // namespace

TEST_CASE("a randomized run keeps its settings and seed through a save and a load") {
  const TestPacks packs;
  RunSettings run = chaos(RunSeed);
  run.learnChance = LearnChance::ThreeQuarters;
  run.items = false;
  const auto game = randomizedGame(packs, run);
  CHECK(game->randomizer().settings() == run);
  const auto saved = game->saveFile();
  REQUIRE_MESSAGE(saved.has_value(), errorOf(saved));
  CHECK(saved->header.summary.randomizer == runCode(run));
  CHECK(summaryFromSections(*saved).randomizer == runCode(run));

  const auto text = dwe::game::save::writeSaveFile(*saved);
  REQUIRE_MESSAGE(text.has_value(), errorOf(text));
  auto parsed = dwe::game::save::parseSaveFile(*text);
  REQUIRE_MESSAGE(parsed.has_value(), errorOf(parsed));
  const auto other = randomizedGame(packs, RunSettings{});
  const auto loaded = other->loadFile(*parsed);
  REQUIRE_MESSAGE(loaded.has_value(), errorOf(loaded));
  CHECK(other->randomizer().settings() == run);
}

TEST_CASE("a game that is no randomized run saves an inactive randomizer and no run code") {
  const TestPacks packs;
  const auto game = randomizedGame(packs, RunSettings{.seed = RunSeed});
  CHECK_FALSE(game->randomizer().settings().active());
  const auto saved = game->saveFile();
  REQUIRE_MESSAGE(saved.has_value(), errorOf(saved));
  CHECK(saved->header.summary.randomizer.empty());
  CHECK(summaryFromSections(*saved).randomizer.empty());
}

TEST_CASE("the randomizer never takes the game's random numbers") {
  const TestPacks packs;
  const auto plain = randomizedGame(packs, RunSettings{});
  const auto off = randomizedGame(packs, RunSettings{.seed = RunSeed});
  RunSettings everything = chaos(RunSeed);
  const auto randomized = randomizedGame(packs, everything);
  for (GameSession* game : {plain.get(), off.get(), randomized.get()}) {
    game->clock().setSpeed(SixtyGameHoursPerSecond);
    careFor(*game, OneGameDay);
  }
  CHECK(off->partner().state() == plain->partner().state());
  CHECK(off->partner().randomState() == plain->partner().randomState());
  // The made-up species have one rookie, so the random starter is the same partner.
  CHECK(randomized->partner().state() == plain->partner().state());
  CHECK(randomized->partner().randomState() == plain->partner().randomState());
}

TEST_CASE("the same seed starts the same partner, a rookie of the partner's own species") {
  const TestPacks packs;
  const auto first = randomizedGame(packs, chaos(RunSeed));
  const auto second = randomizedGame(packs, chaos(RunSeed));
  CHECK(first->partner().state().species == second->partner().state().species);
  CHECK(first->partner().state().species == "agum");
  CHECK(first->roster().size() == 5);
}
