#include <dwe/game/partner/events.hpp>
#include <dwe/game/partner/poop_piles.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/poop_adapter.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "session_fixture.hpp"

using dwe::game::partner::EventKind;
using dwe::game::partner::PartnerEvent;
using dwe::game::partner::Tile;
using dwe::session::GameSession;
using dwe::session::PoopAdapter;
using dwe::session::SessionSystem;
using dwe::session::testing::errorOf;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

constexpr std::string_view Map = "map/test";
constexpr Tile Standing{.x = 40, .y = 40};
// Facing the original's rotation 0 puts the pile one row up (createPoopPile).
constexpr Tile InFront{.x = 40, .y = 39};
constexpr std::int32_t PileSize = 12;

std::unique_ptr<GameSession> sessionWithPoop(const TestPacks& packs, PoopAdapter*& poop) {
  auto adapter = std::make_unique<PoopAdapter>();
  poop = adapter.get();
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(adapter));
  return newGameWith(packs, std::move(systems));
}

} // namespace

TEST_CASE("a pile the partner left waits for the field, which places it in front of the partner") {
  const TestPacks packs;
  PoopAdapter* poop = nullptr;
  auto session = sessionWithPoop(packs, poop);
  const auto& rules = poop->rules();
  CHECK((rules.has_value() && rules->look.modelSize > 0));
  CHECK_FALSE(poop->nextPending().has_value());

  poop->routePartnerEvent(*session,
                          PartnerEvent{.kind = EventKind::PoopedOnGround, .value = PileSize});
  REQUIRE(poop->nextPending().has_value());
  CHECK(poop->nextPending().value_or(dwe::session::PendingPile{}).wild);
  const auto placed = poop->placePending(*session, Map, Standing, 0);
  REQUIRE(placed.has_value());
  CHECK(placed.value_or(Tile{}) == InFront);
  CHECK_FALSE(poop->nextPending().has_value());

  const auto piles = poop->onMap(Map);
  REQUIRE(piles.size() == 1);
  CHECK(piles.front().size == PileSize);
  CHECK(poop->onMap("map/other").empty());
  CHECK(poop->remove(Map, InFront) == PileSize);
  CHECK(poop->onMap(Map).empty());
}

TEST_CASE("the pile of a night's sleep is no scene of the field") {
  const TestPacks packs;
  PoopAdapter* poop = nullptr;
  auto session = sessionWithPoop(packs, poop);
  poop->routePartnerEvent(*session,
                          PartnerEvent{.kind = EventKind::PoopedOnGround, .value = PileSize});
  poop->routePartnerEvent(*session, PartnerEvent{.kind = EventKind::Slept, .value = 0});
  REQUIRE(poop->nextPending().has_value());
  CHECK_FALSE(poop->nextPending().value_or(dwe::session::PendingPile{}).wild);
}

TEST_CASE("the piles go in the save and come back") {
  const TestPacks packs;
  PoopAdapter* saving = nullptr;
  auto saved = sessionWithPoop(packs, saving);
  saving->routePartnerEvent(*saved,
                            PartnerEvent{.kind = EventKind::PoopedOnGround, .value = PileSize});
  REQUIRE(saving->placePending(*saved, Map, Standing, 0).has_value());
  auto file = saved->saveFile();
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));
  REQUIRE(file->sections.contains("poop_piles"));

  PoopAdapter* loading = nullptr;
  auto loaded = sessionWithPoop(packs, loading);
  const auto read = loaded->loadFile(*file);
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  const auto piles = loading->onMap(Map);
  REQUIRE(piles.size() == 1);
  CHECK(piles.front().tile == InFront);
  CHECK(piles.front().size == PileSize);

  REQUIRE(loaded->startNewGame().has_value());
  CHECK(loading->onMap(Map).empty());
}
