#include <dwe/game/inventory/use.hpp>

#include <doctest/doctest.h>

#include <ostream>
#include <string_view>
#include <vector>

#include "fixture.hpp"

using namespace dwe::game::inventory;
using namespace dwe::game::inventory::testing;

namespace {

// A partner that records what it is given.
class RecordingTarget final : public ItemEffectTarget {
public:
  bool refuses = false;
  std::vector<ItemEffect> applied;
  int asked = 0;

  bool accepts(const ItemEffect& /*effect*/, UseContext /*context*/) override {
    ++asked;
    return !refuses;
  }
  void apply(const ItemEffect& effect, UseContext /*context*/) override {
    applied.push_back(effect);
  }
};

struct UseFixture {
  Inventory inventory = originalInventory();
  dwe::assets::ItemTable items = fixtureItems();
  ItemEffects effects = fixtureItemEffects(inventory.rules());
  RecordingTarget target;

  dwe::core::Result<UseResult> tryUse(std::string_view item, UseContext context) {
    return useItem(inventory, handleOf(item), context, items, effects, target);
  }

  UseResult useResult(std::string_view item, UseContext context) {
    const auto result = tryUse(item, context);
    REQUIRE_MESSAGE(result.has_value(), errorOf(result));
    return *result;
  }

  UseOutcome use(std::string_view item, UseContext context) {
    return useResult(item, context).outcome;
  }
};

} // namespace

TEST_CASE("an item used where its colour allows it is consumed and applied to the target") {
  UseFixture fixture;
  fixture.inventory.add(handleOf(Tonic), 2);
  CHECK(fixture.use(Tonic, UseContext::Field) == UseOutcome::Used);
  CHECK(fixture.inventory.countOf(handleOf(Tonic)) == 1);
  REQUIRE(fixture.target.applied.size() == 1);
  CHECK(fixture.target.applied[0].healing.hp == 500);
  CHECK(fixture.use(Tonic, UseContext::Battle) == UseOutcome::Used);
  CHECK(fixture.inventory.countOf(handleOf(Tonic)) == 0);
  CHECK(fixture.use(Tonic, UseContext::Field) == UseOutcome::NotInBag);
}

TEST_CASE("colour 0 is for the field, 1 for battle, 2 for both and 255 for neither") {
  UseFixture fixture;
  for (const std::string_view item : {Ether, Bun, GateKey}) {
    fixture.inventory.add(handleOf(item), 1);
  }
  CHECK(fixture.use(Ether, UseContext::Field) == UseOutcome::NotUsableHere);
  CHECK(fixture.use(Bun, UseContext::Battle) == UseOutcome::NotUsableHere);
  CHECK(fixture.use(GateKey, UseContext::Field) == UseOutcome::NotUsableHere);
  CHECK(fixture.use(GateKey, UseContext::Battle) == UseOutcome::NotUsableHere);
  CHECK(fixture.inventory.countOf(handleOf(Ether)) == 1);
  CHECK(fixture.target.applied.empty());
}

TEST_CASE(
    "in the field the partner may refuse and nothing is consumed; in battle the item is thrown and lands") {
  UseFixture fixture;
  fixture.inventory.add(handleOf(Tonic), 2);
  fixture.target.refuses = true;
  CHECK(fixture.use(Tonic, UseContext::Field) == UseOutcome::Refused);
  CHECK(fixture.inventory.countOf(handleOf(Tonic)) == 2);
  CHECK(fixture.target.applied.empty());
  const int askedInField = fixture.target.asked;
  CHECK(fixture.use(Tonic, UseContext::Battle) == UseOutcome::Used);
  CHECK(fixture.target.asked == askedInField);
  CHECK(fixture.inventory.countOf(handleOf(Tonic)) == 1);
}

TEST_CASE("an item that does nothing is consumed anyway, unless the quirk is off") {
  UseFixture fixture;
  fixture.inventory.add(handleOf(Rod), 1);
  fixture.inventory.add(handleOf(Charm), 1);
  CHECK(fixture.use(Rod, UseContext::Field) == UseOutcome::Used);
  CHECK(fixture.inventory.countOf(handleOf(Rod)) == 0);
  CHECK_FALSE(fixture.target.applied.back().doesSomething());

  InventoryRules fixed = fixture.inventory.rules();
  fixed.quirks.uselessItemsConsumed = false;
  fixture.inventory.setRules(
      fixed, stackingRules().create(*originalRuleset().section(SectionName), StackingPath).value());
  CHECK(fixture.use(Charm, UseContext::Field) == UseOutcome::Useless);
  CHECK(fixture.inventory.countOf(handleOf(Charm)) == 1);
}

TEST_CASE("an item whose effect neither the table nor the ruleset gives is an error, and is kept") {
  UseFixture fixture;
  fixture.inventory.add(handleOf(Hidden), 1);
  const auto outcome = fixture.tryUse(Hidden, UseContext::Field);
  CHECK_FALSE(outcome.has_value());
  CHECK(fixture.inventory.countOf(handleOf(Hidden)) == 1);
}

TEST_CASE(
    "using an item says whether it is a key item, one that can be neither sold nor thrown away") {
  UseFixture fixture;
  fixture.inventory.add(handleOf(Rod), 1);
  fixture.inventory.add(handleOf(Tonic), 1);
  const UseResult rod = fixture.useResult(Rod, UseContext::Field);
  CHECK(rod.outcome == UseOutcome::Used);
  CHECK(rod.keyItem);
  CHECK_FALSE(fixture.useResult(Tonic, UseContext::Field).keyItem);
  // A key item not used is still reported, so the caller knows what it was.
  CHECK(fixture.useResult(GateKey, UseContext::Field).keyItem);
}
