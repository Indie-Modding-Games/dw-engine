#include <dwe/assets/digimon_table.hpp>
#include <dwe/screens/bag_view.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <ostream>

#include "packages/game/inventory/tests/fixture.hpp"

using namespace dwe::game::inventory::testing;

TEST_CASE("the bag view lists the usable slots with the items' names, counts and colour") {
  auto inventory = originalInventory();
  const auto& items = fixtureItems();
  inventory.add(handleOf(Tonic), 3);
  inventory.add(handleOf(GateKey), 1);
  inventory.addMoney(250);
  const dwe::screens::NameKeys names{nullptr, &items, nullptr};

  const auto view = dwe::screens::bagViewOf(inventory, items, names);
  CHECK(view.money == 250);
  REQUIRE(view.slots.size() == static_cast<std::size_t>(inventory.slotCount()));
  // inventory.c: selectOptionMenu: in the field an item with droppable 1 can be discarded.
  CHECK(view.slots[0] == dwe::screens::BagSlotView{.empty = false,
                                                   .item = std::string{Tonic},
                                                   .nameKey = "main.item_names.0000",
                                                   .descriptionKey = "main.item_descs.0000",
                                                   .descriptionSpeciesKey = "",
                                                   .count = 3,
                                                   .dimmed = false,
                                                   .usable = true,
                                                   .discardable = true,
                                                   .itemColor = 2,
                                                   .icon = "/texture/etcdat/item?palette=3",
                                                   .iconRect = "0 0 16 16"});
  CHECK(view.slots[1].item == GateKey);
  CHECK(view.slots[1].dimmed);
  // utils2.c: setItemTexture: the cell of the item's number in rows of 16.
  CHECK(view.slots[1].iconRect == "176 112 16 16");
  CHECK(view.slots[2].empty);

  const auto settings = dwe::screens::settingsOf(view);
  CHECK(std::ranges::any_of(settings, [](const dwe::screens::ScreenSetting& setting) {
    return setting.name == "bag.slot.0.count" && setting.value == dwe::ui::UiValue{3.0};
  }));
}

TEST_CASE("the bag view says which items are used where the bag is open, by their item colour") {
  auto inventory = originalInventory();
  const auto& items = fixtureItems();
  inventory.add(handleOf(Tonic), 1);
  inventory.add(handleOf(Ether), 1);
  inventory.add(handleOf(Bun), 1);
  const dwe::screens::NameKeys names{nullptr, &items, nullptr};
  using dwe::game::inventory::UseContext;

  // inventory.c: selectOptionMenu: the field takes item_color 0 and 2, the battle 1 and 2.
  const auto field = dwe::screens::bagViewOf(inventory, items, names, UseContext::Field);
  CHECK(field.slots[0].usable);
  CHECK_FALSE(field.slots[1].usable);
  CHECK(field.slots[2].usable);
  const auto battle = dwe::screens::bagViewOf(inventory, items, names, UseContext::Battle);
  CHECK(battle.slots[0].usable);
  CHECK(battle.slots[1].usable);
  CHECK_FALSE(battle.slots[2].usable);
}

TEST_CASE("an item's icon is its cell of the items' image, the last column 15 texels wide") {
  using dwe::screens::ItemIconView;
  dwe::assets::ItemEntry entry{.id = "icon", .index = 15, .iconPalette = std::uint8_t{12}};
  // utils2.c: setItemTexture: u 0xF0 draws 15 texels wide.
  CHECK(dwe::screens::itemIconOf(entry) ==
        ItemIconView{.image = "/texture/etcdat/item?palette=12", .rect = "240 0 15 16"});
  entry.index = 33;
  CHECK(dwe::screens::itemIconOf(entry)->rect == "16 32 16 16");
  // No palette (a table extracted before the field existed) or no original number: no icon.
  entry.iconPalette.reset();
  CHECK_FALSE(dwe::screens::itemIconOf(entry).has_value());
  entry.iconPalette = std::uint8_t{1};
  entry.originalIndex = false;
  CHECK_FALSE(dwe::screens::itemIconOf(entry).has_value());
}

TEST_CASE("a pack's item shows an original item's icon and names its form's species by key") {
  // As data/items resolves `icon`: the cell and palette of the item it names.
  const dwe::assets::ItemEntry shown{.id = "my_pack:gem",
                                     .index = 130,
                                     .originalIndex = false,
                                     .iconPalette = std::uint8_t{10},
                                     .iconOf = "red_gem",
                                     .iconCell = 111};
  CHECK(dwe::screens::itemIconOf(shown) ==
        dwe::screens::ItemIconView{.image = "/texture/etcdat/item?palette=10",
                                   .rect = "240 96 15 16"});

  std::vector<dwe::assets::ItemEntry> entries{fixtureItems().entries().begin(),
                                              fixtureItems().entries().end()};
  entries.push_back({.id = "my_pack:gem",
                     .index = 300,
                     .originalIndex = false,
                     .sortingValue = 4,
                     .droppable = 1,
                     .descriptionSpecies = "species_b"});
  const dwe::assets::ItemTable items{std::move(entries)};
  dwe::assets::DigimonTable species;
  dwe::assets::DigimonRow row;
  row.id = "species_b";
  row.index = 7;
  species.digimon.push_back(row);
  auto inventory = originalInventory();
  const auto gem = items.handleOf("my_pack:gem");
  REQUIRE(gem);
  inventory.add(*gem, 1);
  inventory.add(*items.handleOf(Tonic), 1);

  const dwe::screens::NameKeys names{&species, &items, nullptr};
  const auto view = dwe::screens::bagViewOf(inventory, items, names);
  // The pack's own text keys; its description's {species} is the form's name.
  CHECK(view.slots[0].nameKey == "my_pack.item_names.gem");
  CHECK(view.slots[0].descriptionKey == "my_pack.item_descs.gem");
  CHECK(view.slots[0].descriptionSpeciesKey == "main.digimon_names.0007");
  CHECK(view.slots[1].descriptionSpeciesKey.empty());
  const auto settings = dwe::screens::settingsOf(view);
  CHECK(std::ranges::any_of(settings, [](const dwe::screens::ScreenSetting& setting) {
    return setting.name == "bag.slot.0.description_species" &&
           setting.value == dwe::ui::UiValue{std::string{"main.digimon_names.0007"}};
  }));
  // Without the species table the key is empty: the description shows {species} unfilled.
  const dwe::screens::NameKeys noSpecies{nullptr, &items, nullptr};
  CHECK(noSpecies.itemDescriptionSpecies(*gem).empty());
}
