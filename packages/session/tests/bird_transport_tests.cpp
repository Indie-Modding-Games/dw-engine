#include <dwe/game/inventory/inventory.hpp>
#include <dwe/game/story/story_state.hpp>
#include <dwe/session/bird_transport.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/story_adapter.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include "adapter_fixture.hpp"
#include "packages/game/inventory/tests/fixture.hpp"
#include "packages/session/src/story_routines/routines.hpp"
#include "packages/session/src/story_scenes_context.hpp"

namespace items = dwe::game::inventory::testing;
using dwe::game::evolution::testing::TableFixture;
using dwe::session::BirdTransport;
using dwe::session::BirdTransportDesc;
using dwe::session::BirdTransportRules;
using dwe::session::InventoryAdapter;
using dwe::session::InventoryAdapterDesc;
using dwe::session::SessionSystem;
using dwe::session::StoryAdapter;
using dwe::session::StoryAdapterDesc;
using dwe::session::testing::evolutionAdapter;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

// The triggers of BIRDRA_TRANSPORT_TARGETS, in order.
constexpr std::int32_t CanyonTrigger = 0xdd;
constexpr std::int32_t SavannaTrigger = 0xbe;
constexpr std::int32_t DinoTrigger = 0xbc;
constexpr std::int32_t FreezelandTrigger = 0x15f;
constexpr std::int32_t MistyTrigger = 0x93;
constexpr std::int32_t BeetleTrigger = 0xd2;

// The original's transport with a bag, a story and the original ruleset.
struct Transport {
  TestPacks packs;
  TableFixture forms;
  StoryAdapter* story = nullptr;
  InventoryAdapter* inventory = nullptr;
  BirdTransport* transport = nullptr;
  std::unique_ptr<dwe::session::GameSession> session;

  Transport() {
    auto evolution = evolutionAdapter(packs, forms);
    auto bag =
        std::make_unique<InventoryAdapter>(InventoryAdapterDesc{.files = &packs.files(),
                                                                .items = items::fixtureItems(),
                                                                .effects = items::fixtureEffects(),
                                                                .evolution = evolution.get()});
    auto state = std::make_unique<StoryAdapter>(StoryAdapterDesc{.files = &packs.files()});
    auto bird = std::make_unique<BirdTransport>(
        BirdTransportDesc{.files = &packs.files(), .inventory = bag.get(), .story = state.get()});
    story = state.get();
    inventory = bag.get();
    transport = bird.get();
    std::vector<std::unique_ptr<SessionSystem>> systems;
    systems.push_back(std::move(evolution));
    systems.push_back(std::move(bag));
    systems.push_back(std::move(state));
    systems.push_back(std::move(bird));
    session = newGameWith(packs, std::move(systems));
  }

  void setMoney(std::int32_t money) const {
    auto& bag = inventory->inventory();
    bag.spendMoney(bag.money());
    bag.addMoney(money);
  }
};

} // namespace

TEST_CASE("the bird transport reads the original ruleset: MAPHEAD 0x4d6 lines 4-7, six places") {
  const Transport bird;
  REQUIRE(bird.transport->available());
  const BirdTransportRules& rules = bird.transport->rules();
  CHECK(rules.linesSection == 0x4d6);
  CHECK(rules.lines.greeting == 4);
  CHECK(rules.lines.farewell == 5);
  CHECK(rules.lines.confirm == 6);
  CHECK(rules.lines.choices == 7);
  CHECK(rules.bitsSection == 0x4c4);
  CHECK(rules.lines.bits == 6);
  CHECK(rules.mapVariable == 247);
  CHECK(rules.entryVariable == 248);
  CHECK(rules.travelSection == 0x4e3);
  CHECK(rules.choiceLockTrigger == 0x31);
  CHECK(rules.refuseSound.sound == 0xb);
  REQUIRE(rules.destinations.size() == 6);
  CHECK(rules.destinations[2].map == 0x4f);
  CHECK(rules.destinations[2].entry == 9);
  CHECK(rules.destinations[2].trigger == DinoTrigger);
  CHECK(rules.destinations[2].cost == 1500);
  CHECK(rules.destinations[5].map == 0x69);
  CHECK(rules.destinations[5].entry == 0);
}

TEST_CASE("birdraFillTargetList lists the places whose trigger is set, red past the money") {
  // Measured in the original with all six triggers and 1800 bits: the first three white, the
  // last three red.
  const Transport bird;
  for (const std::int32_t trigger : {CanyonTrigger, SavannaTrigger, DinoTrigger, FreezelandTrigger,
                                     MistyTrigger, BeetleTrigger}) {
    REQUIRE(bird.story->state().setTrigger(trigger, true));
  }
  bird.setMoney(1800);
  REQUIRE(bird.transport->open());
  const auto& rows = bird.transport->rows();
  REQUIRE(rows.size() == 6);
  for (std::size_t row = 0; row < rows.size(); ++row) {
    CHECK(rows[row].destination == row);
    CHECK(rows[row].affordable == (row < 3));
  }
  // A place he cannot pay for only sounds.
  CHECK_FALSE(bird.transport->pick(3));
  CHECK_FALSE(bird.transport->picked());

  // Without the canyon's and the savanna's triggers the list starts at the dino region.
  bird.transport->close();
  REQUIRE(bird.story->state().setTrigger(CanyonTrigger, false));
  REQUIRE(bird.story->state().setTrigger(SavannaTrigger, false));
  REQUIRE(bird.transport->open());
  REQUIRE(bird.transport->rows().size() == 4);
  CHECK(bird.transport->rows().front().destination == 2);
}

TEST_CASE("the yes writes the place to variables 247 and 248 and takes its price") {
  // Measured: the dino region with 1800 bits leaves 300, variable 247 0x4f and 248 9, and the
  // script goes on at MAPHEAD 0x4e3 ("Hold on tight!", then the trip to koda00).
  const Transport bird;
  REQUIRE(bird.story->state().setTrigger(DinoTrigger, true));
  bird.setMoney(1800);
  REQUIRE(bird.transport->open());
  REQUIRE(bird.transport->pick(0));
  CHECK(bird.transport->travel() == 0x4e3);
  CHECK(bird.inventory->inventory().money() == 300);
  CHECK(bird.story->state().variable(247).value_or(0) == 0x4f);
  CHECK(bird.story->state().variable(248).value_or(0) == 9);
}

TEST_CASE("a place the story lock keeps closed is red and cannot be picked, so no bits go") {
  // Coordinator's decision (2026-10-31): the port's stage lock must never take the money and leave
  // the player in the carrier's house. With the canyon (map 0x26) open and the dino region (0x4f)
  // closed, the dino region is red although the money pays for it.
  const Transport bird;
  REQUIRE(bird.story->state().setTrigger(CanyonTrigger, true));
  REQUIRE(bird.story->state().setTrigger(DinoTrigger, true));
  bird.setMoney(1800);
  REQUIRE(bird.transport->open([](std::int32_t map) { return map == 0x26; }));
  const auto& rows = bird.transport->rows();
  REQUIRE(rows.size() == 2);
  CHECK(rows[0].pickable());
  CHECK(rows[1].affordable);
  CHECK_FALSE(rows[1].open);
  CHECK_FALSE(rows[1].pickable());
  CHECK_FALSE(bird.transport->pick(1));
  CHECK_FALSE(bird.transport->picked());
  CHECK(bird.inventory->inventory().money() == 1800);
  CHECK(bird.transport->pick(0));
}

TEST_CASE("cancel answers no to the yes-or-no unless trigger 0x31 is set") {
  const Transport bird;
  REQUIRE(bird.transport->open());
  CHECK(bird.transport->choiceCancellable());
  REQUIRE(bird.story->state().setTrigger(0x31, true));
  CHECK_FALSE(bird.transport->choiceCancellable());
}

TEST_CASE("routine 10 waits for the transport and goes on at the section the yes leads to") {
  const Transport bird;
  dwe::session::StoryScenesContext scenes{dwe::session::StoryScenesDesc{.story = bird.story}};
  scenes.rules.operandVariable = 254;
  REQUIRE(dwe::session::registerBirdTransportRoutine(scenes));
  // room12's script 167: set_pstat(254, 5), call_routine(10).
  REQUIRE(bird.story->state().setVariable(254, 5));
  const auto started = scenes.routines.call(10);
  REQUIRE(started);
  const auto* waits = std::get_if<dwe::game::story::RoutineWaits>(&*started);
  REQUIRE(waits != nullptr);
  REQUIRE(scenes.screenRequests.size() == 1);
  CHECK(scenes.screenRequests.front().screen == dwe::session::StoryScreen::BirdTransport);
  CHECK(scenes.screenRequests.front().first == 5);
  CHECK(std::holds_alternative<dwe::game::story::TaskRunning>(waits->task->tick()));
  CHECK(std::holds_alternative<dwe::game::story::TaskRunning>(waits->task->tick()));
  // The yes: the screen closes and the script moves to MAPHEAD 0x4e3.
  scenes.screenOpen = false;
  scenes.screenJump = dwe::game::story::SectionRef{.script = 0, .section = 0x4e3};
  const auto step = waits->task->tick();
  const auto* jump = std::get_if<dwe::game::story::TaskJump>(&step);
  REQUIRE(jump != nullptr);
  CHECK(jump->target.script == 0);
  CHECK(jump->target.section == 0x4e3);
  CHECK_FALSE(scenes.screenJump);

  // Without a trip the script goes on after the call.
  const auto again = scenes.routines.call(10);
  REQUIRE(again);
  const auto& task = std::get<dwe::game::story::RoutineWaits>(*again).task;
  CHECK(std::holds_alternative<dwe::game::story::TaskRunning>(task->tick()));
  scenes.screenOpen = false;
  CHECK(std::holds_alternative<dwe::game::story::TaskDone>(task->tick()));
}
