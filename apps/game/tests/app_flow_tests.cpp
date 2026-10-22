#include <dwe/core/error.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>
#include <dwe/session/game_session.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <ostream>

#include "app_flow.hpp"
#include "session_fixture.hpp"

namespace {

using dwe::app::AppFlow;
using dwe::app::PlayStart;
using dwe::game::save::MemorySaveStorage;
using dwe::game::save::SlotId;
using dwe::game::save::SlotKind;
using dwe::session::GameSession;
using dwe::session::testing::careFor;
using dwe::session::testing::errorOf;
using dwe::session::testing::newGame;
using dwe::session::testing::TestPacks;

constexpr SlotId FirstSlot{.kind = SlotKind::Manual, .index = 1};
constexpr SlotId EmptySlot{.kind = SlotKind::Manual, .index = 2};

// The app flow over bare sessions: a new game, or the save in a slot of `storage`.
AppFlow<GameSession> flowOn(const TestPacks& packs, MemorySaveStorage& storage) {
  return AppFlow<GameSession>{
      [&packs,
       &storage](const PlayStart& start) -> dwe::core::Result<std::unique_ptr<GameSession>> {
        auto session = newGame(packs, &storage);
        if (start.load) {
          if (auto loaded = session->load(*start.load); !loaded) {
            return std::unexpected{loaded.error()};
          }
        }
        return session;
      }};
}

std::int64_t freshClock(const TestPacks& packs) {
  MemorySaveStorage storage;
  return newGame(packs, &storage)->clock().elapsedFrames();
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
  careFor(*flow.playing(), 600.0);
  const auto saved = flow.playing()->save(FirstSlot);
  REQUIRE_MESSAGE(saved.has_value(), errorOf(saved));
  const std::int64_t savedClock = flow.playing()->clock().elapsedFrames();
  careFor(*flow.playing(), 600.0);
  REQUIRE(savedClock != freshClock(packs));

  flow.toTitle();
  CHECK(flow.playing() == nullptr);

  const auto newOne = flow.play({});
  REQUIRE_MESSAGE(newOne.has_value(), errorOf(newOne));
  CHECK(flow.playing()->clock().elapsedFrames() == freshClock(packs));
  CHECK_FALSE(flow.playing()->homeSlot().has_value());

  const auto loaded = flow.play({.load = FirstSlot});
  REQUIRE_MESSAGE(loaded.has_value(), errorOf(loaded));
  CHECK(flow.playing()->clock().elapsedFrames() == savedClock);
  CHECK(flow.playing()->homeSlot() == FirstSlot);
}

TEST_CASE("a load that fails leaves the app at the title screen") {
  const TestPacks packs;
  MemorySaveStorage storage;
  auto flow = flowOn(packs, storage);
  const auto started = flow.play({});
  REQUIRE_MESSAGE(started.has_value(), errorOf(started));

  CHECK_FALSE(flow.play({.load = EmptySlot}).has_value());
  CHECK(flow.playing() == nullptr);
  const auto file = storage.read("slot_02.yaml");
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));
  CHECK_FALSE(file->has_value());
}
