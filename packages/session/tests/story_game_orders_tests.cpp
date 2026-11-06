#include <dwe/game/partner/state.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_game_orders.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "session_fixture.hpp"

using dwe::session::SessionSystem;
using dwe::session::StoryAdapter;
using dwe::session::StoryGameOrders;
using dwe::session::testing::errorOf;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

// The game pack's story rules (rules/original/story.yaml): while a script holds the player still,
// variable 0 holds 3.
constexpr std::int64_t FrozenVariable = 0;
constexpr std::int64_t FrozenValue = 3;

struct OrdersSession {
  std::unique_ptr<dwe::session::GameSession> session;
  StoryAdapter* story = nullptr;
  StoryGameOrders* orders = nullptr;
};

OrdersSession ordersSession(const TestPacks& packs) {
  auto adapter =
      std::make_unique<StoryAdapter>(dwe::session::StoryAdapterDesc{.files = &packs.files()});
  auto orders = std::make_unique<StoryGameOrders>(
      dwe::session::StoryGameOrdersDesc{.files = &packs.files(), .story = adapter.get()});
  OrdersSession made{.session = nullptr, .story = adapter.get(), .orders = orders.get()};
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(adapter));
  systems.push_back(std::move(orders));
  made.session = newGameWith(packs, std::move(systems));
  return made;
}

void setHp(dwe::session::GameSession& session, std::int32_t maximum, std::int32_t current) {
  dwe::game::partner::PartnerState partner = session.partner().state();
  partner.stats.base[dwe::game::partner::Stat::Hp] = maximum;
  partner.stats.currentHp = current;
  session.partner().takeStoryChange(std::move(partner));
}

void run(StoryAdapter& story, std::string_view code) {
  REQUIRE(story.vm() != nullptr);
  const auto ran = story.vm()->runString(code, "deal_damage test");
  REQUIRE_MESSAGE(ran.has_value(), errorOf(ran));
}

} // namespace

TEST_CASE("deal_damage freezes the world, takes the percent of the maximum HP and shows it") {
  const TestPacks packs;
  const OrdersSession game = ordersSession(packs);
  setHp(*game.session, 800, 800);
  REQUIRE(game.story->state().variable(FrozenVariable).value_or(-1) != FrozenValue);
  // Elecmon's shock (gias02's script 68, section 5): set_pstat(200, 20), deal_damage(200).
  run(*game.story, "story.set_var(200, 20)\nstory.deal_damage(200)");
  // script_interp.c: scriptPauseGame(0xff) first: the player stays where the script puts him.
  CHECK(game.story->state().variable(FrozenVariable).value_or(-1) == FrozenValue);
  CHECK(game.session->partner().state().stats.currentHp == 640);
  // addEntityText(partner, 0, 0, 160, 0): the field shows the number once.
  CHECK(game.orders->takePartnerDamage() == std::vector<std::int32_t>{160});
  CHECK(game.orders->takePartnerDamage().empty());
  // Three shocks: 800, 640, 480, 320 (measured in the original).
  run(*game.story, "story.deal_damage(200)\nstory.deal_damage(200)");
  CHECK(game.session->partner().state().stats.currentHp == 320);
  CHECK(game.orders->takePartnerDamage() == std::vector<std::int32_t>{160, 160});
}

TEST_CASE("deal_damage leaves at least 1 HP and shows the damage it did") {
  const TestPacks packs;
  const OrdersSession game = ordersSession(packs);
  setHp(*game.session, 800, 100);
  run(*game.story, "story.set_var(200, 20)\nstory.deal_damage(200)");
  CHECK(game.session->partner().state().stats.currentHp == 1);
  CHECK(game.orders->takePartnerDamage() == std::vector<std::int32_t>{99});
}
