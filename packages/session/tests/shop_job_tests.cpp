#include <dwe/assets/item_table.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/item_keeper.hpp>
#include <dwe/session/shop_job_adapter.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_scenes.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "packages/game/inventory/tests/fixture.hpp"
#include "packages/session/src/story_routines/routines.hpp"
#include "packages/session/src/story_scenes_context.hpp"
#include "session_fixture.hpp"

namespace {

namespace items = dwe::game::inventory::testing;
namespace shop_job = dwe::game::shop_job;
using dwe::session::GameSession;
using dwe::session::InventoryAdapter;
using dwe::session::InventoryAdapterDesc;
using dwe::session::ItemKeeper;
using dwe::session::ItemKeeperDesc;
using dwe::session::SessionSystem;
using dwe::session::ShopJobAction;
using dwe::session::ShopJobAdapter;
using dwe::session::ShopJobAdapterDesc;
using dwe::session::ShopJobTurn;
using dwe::session::StoryAdapter;
using dwe::session::StoryAdapterDesc;
using dwe::session::testing::errorOf;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

constexpr std::string_view BonusItem = "game:steel_heart";
constexpr std::int32_t Customers = 123;
constexpr std::int32_t Result = 110;
constexpr std::int32_t Level = 117;
constexpr std::int32_t Face = 248;

dwe::assets::ItemTable withBonusItem() {
  const auto fixture = items::fixtureItems().entries();
  std::vector<dwe::assets::ItemEntry> entries{fixture.begin(), fixture.end()};
  entries.push_back({.id = std::string{BonusItem},
                     .index = static_cast<std::int32_t>(entries.size()) + 1'000,
                     .originalIndex = false,
                     .value = 9999,
                     .meritValue = 0,
                     .sortingValue = 4,
                     .itemColor = 0,
                     .droppable = 1});
  return dwe::assets::ItemTable{std::move(entries)};
}

// A game with the story, the bag and the improved job, on the game pack's rules.
struct Job {
  std::unique_ptr<GameSession> session;
  StoryAdapter* story = nullptr;
  InventoryAdapter* inventory = nullptr;
  ItemKeeper* keeper = nullptr;
  ShopJobAdapter* job = nullptr;
};

Job play(const TestPacks& packs, std::uint64_t seed) {
  auto inventory =
      std::make_unique<InventoryAdapter>(InventoryAdapterDesc{.files = &packs.files(),
                                                              .items = withBonusItem(),
                                                              .effects = items::fixtureEffects(),
                                                              .evolution = nullptr});
  auto story = std::make_unique<StoryAdapter>(StoryAdapterDesc{.files = &packs.files()});
  auto keeper = std::make_unique<ItemKeeper>(
      ItemKeeperDesc{.files = &packs.files(), .inventory = inventory.get()});
  auto job = std::make_unique<ShopJobAdapter>(ShopJobAdapterDesc{
      .story = story.get(), .inventory = inventory.get(), .keeper = keeper.get()});
  Job played{.session = nullptr,
             .story = story.get(),
             .inventory = inventory.get(),
             .keeper = keeper.get(),
             .job = job.get()};
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(inventory));
  systems.push_back(std::move(keeper));
  systems.push_back(std::move(story));
  systems.push_back(std::move(job));
  played.session = newGameWith(packs, std::move(systems), nullptr, seed);
  return played;
}

dwe::game::save::SaveFile throughText(const dwe::game::save::SaveFile& file) {
  const auto text = dwe::game::save::writeSaveFile(file);
  REQUIRE_MESSAGE(text.has_value(), errorOf(text));
  auto read = dwe::game::save::parseSaveFile(*text);
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  return std::move(*read);
}

std::int64_t variable(const Job& job, std::int32_t number) {
  return job.story->state().variable(number).value_or(-1);
}

// The scenes' context with the engine's shop job routines and gcan11's customer count.
std::unique_ptr<dwe::session::StoryScenesContext> scenesFor(Job& job) {
  auto scenes = std::make_unique<dwe::session::StoryScenesContext>(
      dwe::session::StoryScenesDesc{.story = job.story});
  scenes->session = job.session.get();
  scenes->shopJob = job.job;
  scenes->rules.shopJob = dwe::session::ShopJobRules{.map = "gcan11",
                                                     .customersVariable = Customers,
                                                     .customers = 20,
                                                     .betterPayFrom = 7,
                                                     .joinsFrom = 12};
  REQUIRE(dwe::session::registerShopJobRoutines(*scenes));
  return scenes;
}

} // namespace

TEST_CASE("the improved shop job's line comes back the same after a save and a load") {
  const TestPacks packs;
  Job saving = play(packs, 21);
  REQUIRE(saving.job->rules() != nullptr);
  saving.job->startJob(saving.session->seed());
  REQUIRE(saving.job->lineup() != nullptr);
  const std::vector<shop_job::Customer> line = saving.job->lineup()->customers;
  const auto file = saving.session->saveFile();
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));
  REQUIRE(file->sections.contains(ShopJobAdapter::SaveName));

  Job loading = play(packs, 21);
  const auto read = loading.session->loadFile(throughText(*file));
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  REQUIRE(loading.job->lineup() != nullptr);
  CHECK(loading.job->lineup()->customers == line);

  // The next job draws another line.
  loading.job->startJob(loading.session->seed());
  CHECK(loading.job->lineup()->customers != line);
}

TEST_CASE("routines 256 to 259 tell the story the mode, the next customer and a counter turn") {
  const TestPacks packs;
  Job job = play(packs, 5);
  auto scenes = scenesFor(job);
  REQUIRE(scenes->routines.call(256));
  CHECK(variable(job, Result) == 1);
  job.job->setImproved(false);
  REQUIRE(scenes->routines.call(256));
  CHECK(variable(job, Result) == 0);
  job.job->setImproved(true);

  // The first customer makes the line and names its script.
  REQUIRE(job.story->state().setVariable(Customers, 0));
  REQUIRE(scenes->routines.call(257));
  REQUIRE(job.job->lineup() != nullptr);
  const shop_job::Customer first = job.job->lineup()->customers.front();
  CHECK(variable(job, Result) == job.job->rules()->species[first.species].script);
  // The script the original's draw picked asks again before its customer comes in: the same line.
  const std::vector<shop_job::Customer> line = job.job->lineup()->customers;
  REQUIRE(scenes->routines.call(257));
  CHECK(job.job->lineup()->customers == line);

  // It comes in (the story counts it) and asks for its item.
  REQUIRE(job.story->state().setVariable(Customers, 1));
  REQUIRE(scenes->routines.call(258));
  CHECK(variable(job, Result) == static_cast<std::int64_t>(first.item));
  REQUIRE(job.job->haggle() != nullptr);

  // The counter waits for the app's screen.
  REQUIRE(scenes->routines.call(259));
  REQUIRE(scenes->screenRequests.size() == 1);
  CHECK(scenes->screenRequests.front().screen == dwe::session::StoryScreen::ShopHaggle);
  CHECK(scenes->screenRequests.front().waits);
  const shop_job::Level offer = first.budget == shop_job::NoSale ? 8 : first.budget;
  REQUIRE(job.job->play(ShopJobAction::Offer, offer));
  CHECK_FALSE(job.job->play(ShopJobAction::Talk));
  job.story->setShopTally(100);
  REQUIRE(job.job->finishTurn());
  if (first.budget == shop_job::NoSale) {
    CHECK(variable(job, Result) != static_cast<std::int64_t>(ShopJobTurn::Sold));
  } else {
    CHECK(variable(job, Result) == static_cast<std::int64_t>(ShopJobTurn::Sold));
    CHECK(job.story->shopTally() == 100 + job.job->rules()->profit(first.item, offer));
    CHECK(job.job->haggle() == nullptr);
  }
  CHECK(variable(job, Level) == offer + 1);
  CHECK(variable(job, Face) >= 0);
}

TEST_CASE("a dismissed customer leaves at once and the original mode ignores the routines") {
  const TestPacks packs;
  Job job = play(packs, 9);
  auto scenes = scenesFor(job);
  REQUIRE(job.story->state().setVariable(Customers, 0));
  REQUIRE(scenes->routines.call(257));
  REQUIRE(job.story->state().setVariable(Customers, 1));
  REQUIRE(scenes->routines.call(258));
  REQUIRE(job.job->play(ShopJobAction::Dismiss));
  REQUIRE(job.job->finishTurn());
  CHECK(variable(job, Result) == static_cast<std::int64_t>(ShopJobTurn::Dismissed));
  CHECK(job.job->haggle() == nullptr);

  job.job->setImproved(false);
  REQUIRE(scenes->routines.call(259));
  CHECK(scenes->screenRequests.empty());
  CHECK(variable(job, Result) == static_cast<std::int64_t>(ShopJobTurn::Dismissed));
}

TEST_CASE("routine 260 gives the bonus item from a tally of 3584") {
  const TestPacks packs;
  Job job = play(packs, 3);
  auto scenes = scenesFor(job);
  const auto item = job.inventory->items().handleOf(BonusItem);
  REQUIRE(item.has_value());
  job.story->setShopTally(3583);
  REQUIRE(scenes->routines.call(260));
  CHECK(variable(job, Result) == 0);
  CHECK(job.inventory->inventory().countOf(*item) == 0);
  job.story->setShopTally(3584);
  REQUIRE(scenes->routines.call(260));
  CHECK(variable(job, Result) == 1);
  CHECK(job.inventory->inventory().countOf(*item) == 1);
}

TEST_CASE("with a full bag the bonus item goes to the item keeper") {
  const TestPacks packs;
  Job job = play(packs, 3);
  auto scenes = scenesFor(job);
  const auto item = job.inventory->items().handleOf(BonusItem);
  REQUIRE(item.has_value());
  dwe::game::inventory::Inventory& bag = job.inventory->inventory();
  for (const dwe::assets::ItemEntry& entry : job.inventory->items().entries()) {
    if (entry.id != BonusItem && bag.hasRoomFor(*item)) {
      bag.add(*job.inventory->items().handleOf(entry.id), bag.rules().bag.maxStack);
    }
  }
  REQUIRE_FALSE(bag.hasRoomFor(*item));
  job.story->setShopTally(3584);
  REQUIRE(scenes->routines.call(260));
  CHECK(variable(job, Result) == 2);
  CHECK(bag.countOf(*item) == 0);
  CHECK(job.keeper->keptCount(*item) == 1);

  // A keeper already holding its most of the item: the bonus has nowhere to go.
  REQUIRE(job.keeper->keep(*item, job.keeper->rules().maxCount - 1));
  REQUIRE(scenes->routines.call(260));
  CHECK(variable(job, Result) == 3);
  CHECK(job.keeper->keptCount(*item) == job.keeper->rules().maxCount);
}
