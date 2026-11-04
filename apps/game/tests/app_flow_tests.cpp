#include <dwe/core/error.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/session_system.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "app_flow.hpp"
#include "game/evolution/tests/fixture.hpp"
#include "game/inventory/tests/fixture.hpp"
#include "session_fixture.hpp"

namespace {

namespace items = dwe::game::inventory::testing;
using dwe::app::AppFlow;
using dwe::app::PlayStart;
using dwe::game::evolution::testing::TableFixture;
using dwe::game::save::MemorySaveStorage;
using dwe::game::save::SlotId;
using dwe::game::save::SlotKind;
using dwe::session::GameSession;
using dwe::session::InventoryAdapter;
using dwe::session::InventoryAdapterDesc;
using dwe::session::SessionSystem;
using dwe::session::testing::careFor;
using dwe::session::testing::errorOf;
using dwe::session::testing::evolutionAdapter;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

constexpr SlotId FirstSlot{.kind = SlotKind::Manual, .index = 1};
constexpr SlotId EmptySlot{.kind = SlotKind::Manual, .index = 2};

// A game as the flow holds it: a session with its bag. It counts the games alive, so a test can
// see that two never are.
struct Game {
  explicit Game(std::unique_ptr<GameSession> played, InventoryAdapter* bag)
      : session(std::move(played)), inventory(bag) {
    ++alive;
  }
  Game(const Game&) = delete;
  Game& operator=(const Game&) = delete;
  Game(Game&&) = delete;
  Game& operator=(Game&&) = delete;
  ~Game() { --alive; }

  [[nodiscard]] dwe::core::Result<void> saveTo(SlotId slot) const { return session->save(slot); }

  [[nodiscard]] std::int32_t tonics() const {
    return inventory->inventory().countOf(items::handleOf(items::Tonic));
  }

  static inline int alive = 0;
  std::unique_ptr<GameSession> session;
  InventoryAdapter* inventory;
};

// The app flow over bare sessions with a bag: a new game, or the save in a slot of `storage`.
// `whileStarting` runs while a game starts.
AppFlow<Game> flowOn(const TestPacks& packs, MemorySaveStorage& storage,
                     std::function<void()> whileStarting = {}) {
  return AppFlow<Game>{[&packs, &storage, whileStarting = std::move(whileStarting)](
                           const PlayStart& start) -> dwe::core::Result<std::unique_ptr<Game>> {
    if (whileStarting) {
      whileStarting();
    }
    const TableFixture forms;
    auto evolution = evolutionAdapter(packs, forms);
    auto inventory =
        std::make_unique<InventoryAdapter>(InventoryAdapterDesc{.files = &packs.files(),
                                                                .items = items::fixtureItems(),
                                                                .effects = items::fixtureEffects(),
                                                                .evolution = evolution.get()});
    InventoryAdapter* bag = inventory.get();
    std::vector<std::unique_ptr<SessionSystem>> systems;
    systems.push_back(std::move(evolution));
    systems.push_back(std::move(inventory));
    auto session = newGameWith(packs, std::move(systems), &storage);
    if (start.load) {
      if (auto loaded = session->load(*start.load); !loaded) {
        return std::unexpected{loaded.error()};
      }
    }
    return std::make_unique<Game>(std::move(session), bag);
  }};
}

std::int64_t freshClock(const TestPacks& packs) {
  MemorySaveStorage storage;
  auto flow = flowOn(packs, storage);
  REQUIRE(flow.play({}).has_value());
  return flow.playing()->session->clock().elapsedFrames();
}

} // namespace

TEST_CASE("the app goes from the title screen to a game and back with nothing left behind") {
  const TestPacks packs;
  MemorySaveStorage storage;
  auto flow = flowOn(packs, storage);
  CHECK(flow.playing() == nullptr);

  const auto first = flow.play({});
  REQUIRE_MESSAGE(first.has_value(), errorOf(first));
  REQUIRE(flow.playing() != nullptr);
  GameSession& played = *flow.playing()->session;
  flow.playing()->inventory->inventory().add(items::handleOf(items::Tonic), 1);
  careFor(played, 600.0);
  const auto saved = played.save(FirstSlot);
  REQUIRE_MESSAGE(saved.has_value(), errorOf(saved));
  const std::int64_t savedClock = played.clock().elapsedFrames();
  flow.playing()->inventory->inventory().add(items::handleOf(items::Tonic), 2);
  careFor(played, 600.0);
  REQUIRE(savedClock != freshClock(packs));
  // Playing on autosaved, to the game's own slot: the save and the autosave are one save.
  const std::optional<dwe::session::Autosave> autosaved = played.takeAutosaved();
  REQUIRE(autosaved.has_value());
  CHECK(autosaved->slot == FirstSlot);

  flow.toTitle();
  CHECK(flow.playing() == nullptr);
  CHECK(Game::alive == 0);

  const auto newOne = flow.play({});
  REQUIRE_MESSAGE(newOne.has_value(), errorOf(newOne));
  CHECK(flow.playing()->session->clock().elapsedFrames() == freshClock(packs));
  CHECK_FALSE(flow.playing()->session->homeSlot().has_value());
  CHECK(flow.playing()->tonics() == 0);

  const auto loaded = flow.play(
      {.load = FirstSlot, .backup = false, .saveTo = std::nullopt, .choice = std::nullopt});
  REQUIRE_MESSAGE(loaded.has_value(), errorOf(loaded));
  // The slot has the later state, the autosave's.
  CHECK(flow.playing()->session->clock().elapsedFrames() > savedClock);
  CHECK(flow.playing()->session->homeSlot() == FirstSlot);
  CHECK(flow.playing()->tonics() == 3);
  CHECK(Game::alive == 1);
}

TEST_CASE("a load that fails leaves the app at the title screen, from the title or a game") {
  const TestPacks packs;
  MemorySaveStorage storage;
  auto flow = flowOn(packs, storage);

  CHECK_FALSE(
      flow.play(
              {.load = EmptySlot, .backup = false, .saveTo = std::nullopt, .choice = std::nullopt})
          .has_value());
  CHECK(flow.playing() == nullptr);
  CHECK_FALSE(flow.saveFailed());

  const auto started = flow.play({});
  REQUIRE_MESSAGE(started.has_value(), errorOf(started));
  CHECK_FALSE(
      flow.play(
              {.load = EmptySlot, .backup = false, .saveTo = std::nullopt, .choice = std::nullopt})
          .has_value());
  CHECK(flow.playing() == nullptr);
  CHECK(Game::alive == 0);
  const auto file = storage.read("slot_02.yaml");
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));
  CHECK_FALSE(file->has_value());
}

TEST_CASE("a new game in a slot is saved to it as it starts, or does not start") {
  const TestPacks packs;
  MemorySaveStorage storage;
  auto flow = flowOn(packs, storage);
  const PlayStart inFirst{
      .load = std::nullopt, .backup = false, .saveTo = FirstSlot, .choice = std::nullopt};

  storage.failNextWrite();
  CHECK_FALSE(flow.play(inFirst).has_value());
  CHECK(flow.saveFailed());
  CHECK(flow.playing() == nullptr);
  CHECK(Game::alive == 0);
  CHECK_FALSE(storage.read("slot_01.yaml").value().has_value());

  const auto started = flow.play(inFirst);
  REQUIRE_MESSAGE(started.has_value(), errorOf(started));
  CHECK_FALSE(flow.saveFailed());
  CHECK(flow.playing()->session->homeSlot() == FirstSlot);
  const std::string text = storage.read("slot_01.yaml").value().value_or("");
  CHECK(dwe::game::save::parseSaveFile(text).has_value());
  flow.toTitle();
}

TEST_CASE("the game being played is gone before the next one starts") {
  const TestPacks packs;
  MemorySaveStorage storage;
  std::vector<int> aliveWhileStarting;
  auto flow =
      flowOn(packs, storage, [&aliveWhileStarting] { aliveWhileStarting.push_back(Game::alive); });
  REQUIRE(flow.play({}).has_value());
  REQUIRE(flow.play({}).has_value());
  CHECK(aliveWhileStarting == std::vector<int>{0, 0});
  flow.toTitle();
}
