// A guest's battle HUD over real links (coop-combat.md §7.1): the host's battle, sent as its begin
// and frames, draws on the guest's battle document as the host's own HUD draws it.

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/battle/text_keys.hpp>
#include <dwe/game/coop/battle.hpp>
#include <dwe/game/coop/protocol.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/screens/bag_view.hpp>
#include <dwe/screens/battle_view.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "battle_screen.hpp"
#include "coop_battle_view.hpp"
#include "packages/game/battle/tests/fixture.hpp"
#include "packages/game/coop/tests/support/coop_room.hpp"
#include "partner_menu.hpp"

namespace coop = dwe::game::coop;
using dwe::game::battle::Battle;
using dwe::game::battle::BattleSetup;
using dwe::game::battle::BattleSystemId;
using dwe::game::battle::FighterId;
using dwe::screens::BattleView;
using dwe::screens::NameKeys;

namespace {

constexpr coop::PlayerId Guest = 1;

dwe::assets::DigimonTable speciesTable() {
  dwe::assets::DigimonTable table;
  for (const auto& [id, index] :
       std::vector<std::pair<std::string, std::int32_t>>{{"hero", 3}, {"foe", 12}, {"buddy", 5}}) {
    dwe::assets::DigimonRow row;
    row.id = id;
    row.index = index;
    table.digimon.push_back(row);
  }
  return table;
}

// The host's battle with guest 1's partner as its ally, whose techniques and finisher it orders.
Battle allyBattle(BattleSystemId system) {
  using namespace battle_test;
  BattleSetup setup =
      duel(evenStats(900, 100), evenStats(700, 80), {"punch", "shot", "", "big_finish"});
  setup.system = system;
  setup.allies.push_back(
      {.partner = fighterWith("buddy", evenStats(800, 300), {"punch", "shot", "", "big_finish"},
                              {.x = 300, .z = 0}),
       .condition =
           {.discipline = 50, .tiredness = 0, .sick = false, .injured = false, .knownMoves = {}},
       .tamer = {.x = 300, .z = -800},
       .owner = Guest});
  return startBattle(std::move(setup), 9);
}

coop::BattleBegin beginOf(const Battle& battle) {
  std::vector<std::string> species;
  for (const auto& fighter : battle.state().fighters) {
    species.push_back(fighter.combatant.species);
  }
  return {.battle = 1,
          .starter = coop::HostPlayerId,
          .ask = 0,
          .mapId = "m",
          .enemyNpcs = {5},
          .species = std::move(species),
          .fixedFight = false,
          .allies = {Guest},
          .folder = battle.system() == BattleSystemId::Folder,
          .moves = dwe::app::coopBattleMovesOf(battle)};
}

template <typename T>
std::optional<T> receiveOf(coop::CoopLink& link) {
  while (auto message = link.receive()) {
    if (auto* found = std::get_if<T>(&*message)) {
      return std::move(*found);
    }
  }
  return std::nullopt;
}

// Guest 1 of a room, whose mirror follows the host's battle as the game's does.
struct GuestSide {
  coop::test::CoopRoom room{{.guests = 1}};
  coop::BattleMirror mirror{Guest};
  std::uint32_t frames = 0;

  GuestSide() { REQUIRE(room.connect()); }

  void begin(const coop::BattleBegin& begin) {
    REQUIRE(room.host(Guest).send(coop::Message{begin}));
    room.pump();
    const auto received = receiveOf<coop::BattleBegin>(room.guest(Guest));
    REQUIRE(received);
    mirror.receive(*received);
  }

  // The host's frame for this guest, over the link, as its HUD draws it.
  BattleView see(const Battle& battle, FighterId own, const NameKeys& names,
                 std::optional<std::size_t> throwTarget = std::nullopt,
                 const std::function<void(coop::BattleFrame&)>& edit = {}) {
    coop::BattleFrame frame = dwe::app::coopBattleFrameOf(battle, own);
    if (edit) {
      edit(frame);
    }
    frame.battle = mirror.running()->battle;
    frame.frame = ++frames;
    frame.phase = coop::BattlePhase::Fight;
    REQUIRE(room.host(Guest).send(coop::Message{frame}));
    room.pump();
    const auto received = receiveOf<coop::BattleFrame>(room.guest(Guest));
    REQUIRE(received);
    mirror.receive(*received);
    REQUIRE(mirror.frame());
    return dwe::app::coopBattleViewOf({.begin = &*mirror.running(),
                                       .frame = &*mirror.frame(),
                                       .fighters = mirror.frame()->fighters,
                                       .role = *mirror.role(),
                                       .self = Guest,
                                       .partner = nullptr,
                                       .moves = nullptr,
                                       .bagOpen = false,
                                       .throwTarget = throwTarget},
                                      names);
  }
};

// The game pack's battle document and bag (the partner menu) at 1280x720, as a guest's game shows
// them over the host's battle.
struct BattleUi {
  dwe::assets::MemoryFileSystem memory;
  std::unique_ptr<dwe::assets::VirtualFileSystem> files;
  std::unique_ptr<dwe::localization::Catalog> texts;
  std::unique_ptr<dwe::ui::Ui> ui;
  dwe::input::InputState input{dwe::input::defaultActionMap()};

  BattleUi() {
    const std::filesystem::path pack{DWE_GAME_PACK};
    for (const auto& entry : std::filesystem::recursive_directory_iterator{pack}) {
      if (entry.is_regular_file()) {
        std::ifstream stream{entry.path(), std::ios::binary};
        memory.addFile(
            std::filesystem::path{"packs/game"} / entry.path().lexically_relative(pack),
            std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}});
      }
    }
    memory.addFile("packs/base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\npriority: 0\n");
    const auto location = [this](std::string_view root) {
      return dwe::assets::PackLocation{.fileSystem = &memory,
                                       .root = std::string{root},
                                       .origin = dwe::assets::PackOrigin::Engine};
    };
    const std::array locations{location("packs/base"), location("packs/game")};
    auto mounted = dwe::assets::VirtualFileSystem::mount(locations);
    REQUIRE_MESSAGE(mounted.has_value(), mounted.error().message);
    files = std::make_unique<dwe::assets::VirtualFileSystem>(std::move(*mounted));
    auto catalog = dwe::localization::Catalog::load(*files, "en");
    REQUIRE_MESSAGE(catalog.has_value(), catalog.error().message);
    texts = std::make_unique<dwe::localization::Catalog>(std::move(*catalog));
    auto created = dwe::ui::Ui::create(dwe::ui::UiDesc{.files = files.get(),
                                                       .texts = texts.get(),
                                                       .width = 1280,
                                                       .height = 720,
                                                       .clock = {},
                                                       .scripts = {},
                                                       .cursor = {}});
    REQUIRE_MESSAGE(created.has_value(), created.error().message);
    ui = std::move(*created);
    for (const std::string_view name : {dwe::app::BattleDocument, dwe::app::PartnerMenuDocument}) {
      auto asset = dwe::assets::AssetId::parse("ui/" + std::string{name} + "/document");
      REQUIRE_MESSAGE(asset.has_value(), asset.error().message);
      auto loaded = ui->loadDocument(name, *asset);
      REQUIRE_MESSAGE(loaded.has_value(), loaded.error().message);
    }
  }

  void frames(int count) {
    for (int frame = 0; frame < count; ++frame) {
      ui->tick(input, 1.0 / 60.0);
      ui->update();
      input.endTick();
    }
  }

  // The guest's HUD as CoopBattles::publishHud shows it.
  void showHud(const BattleView& view) {
    dwe::screens::publish(*ui, dwe::screens::settingsOf(view), dwe::screens::BattleRevisionSetting);
    ui->setSetting(dwe::app::BattlePhaseSetting, std::string{dwe::app::FightPhase});
    if (!ui->isVisible(dwe::app::BattleDocument)) {
      ui->setSetting(dwe::app::BattleActiveSetting, true);
      ui->show(dwe::app::BattleDocument, true);
    }
  }
};

dwe::screens::ScreenSettings bagOfMeat(std::int32_t count) {
  dwe::screens::BagView bag;
  bag.slots.push_back({.empty = false,
                       .item = "meat",
                       .nameKey = "meat",
                       .descriptionKey = "meat",
                       .descriptionSpeciesKey = {},
                       .count = count,
                       .dimmed = false,
                       .usable = true,
                       .discardable = false,
                       .itemColor = 1,
                       .icon = {},
                       .iconRect = {},
                       .favorite = false});
  return dwe::screens::settingsOf(bag);
}

void checkSameOrders(const BattleView& guest, const BattleView& host) {
  REQUIRE(guest.allies.size() == host.allies.size());
  for (std::size_t row = 0; row < host.allies.size(); ++row) {
    CHECK(guest.allies[row].orderKey == host.allies[row].orderKey);
  }
}

void checkSameGauge(const BattleView& guest, const BattleView& host) {
  CHECK(host.finisher.goal > 0);
  CHECK(guest.finisher.progress == host.finisher.progress);
  CHECK(guest.finisher.goal == host.finisher.goal);
  CHECK(guest.finisher.ready == host.finisher.ready);
}

} // namespace

TEST_CASE("a guest's battle HUD names the partners' orders and fills its finisher gauge") {
  const auto species = speciesTable();
  const NameKeys names{&species, nullptr, &battle_test::testCatalog()};
  Battle battle = allyBattle(BattleSystemId::Classic);
  const FighterId ally = battle.state().allies.front().id;
  static_cast<void>(battle.command(dwe::game::battle::Command::Technique2));
  for (int step = 0; step < 90; ++step) {
    battle.step();
  }
  GuestSide guest;
  guest.begin(beginOf(battle));
  const BattleView host = dwe::screens::battleViewOf(battle, names, ally);
  const BattleView seen = guest.see(battle, ally, names);
  REQUIRE(host.allies.size() == 1);
  CHECK_FALSE(host.allies.front().orderKey.empty());
  checkSameOrders(seen, host);
  checkSameGauge(seen, host);
  CHECK_FALSE(seen.folder.has_value());
}

TEST_CASE("a guest's battle HUD shows its partner's Folder plate as the host reads it") {
  const auto species = speciesTable();
  const NameKeys names{&species, nullptr, &battle_test::testCatalog()};
  Battle battle = allyBattle(BattleSystemId::Folder);
  const FighterId ally = battle.state().allies.front().id;
  REQUIRE(battle.order(1, 1, ally));
  REQUIRE(battle.order(0, 1, ally));
  REQUIRE(battle.order(1, 1, ally));
  REQUIRE(battle.order(0, 1));
  for (int step = 0; step < 12; ++step) {
    battle.step();
  }
  GuestSide guest;
  guest.begin(beginOf(battle));
  BattleView host = dwe::screens::battleViewOf(battle, names, ally);
  BattleView seen = guest.see(battle, ally, names);
  REQUIRE(host.folder.has_value());
  CHECK(!host.folder->moveKey.empty());
  CHECK_FALSE(host.folder->queue.empty());
  CHECK(seen.folder == host.folder);
  const dwe::screens::ScreenSettings settings = dwe::screens::settingsOf(seen);
  CHECK(std::ranges::any_of(settings, [](const dwe::screens::ScreenSetting& setting) {
    const auto* on = std::get_if<bool>(&setting.value);
    return setting.name == dwe::screens::FolderActiveSetting && on != nullptr && *on;
  }));
  REQUIRE(seen.fighters.size() == host.fighters.size());
  for (std::size_t index = 0; index < host.fighters.size(); ++index) {
    CHECK(seen.fighters[index].targeted == host.fighters[index].targeted);
    CHECK(seen.fighters[index].queued == host.fighters[index].queued);
  }
  checkSameOrders(seen, host);
  checkSameGauge(seen, host);

  // The waiting orders called off leave the plate's notice, and a stance shows on its row.
  REQUIRE(battle.cancelOrders(ally));
  REQUIRE(battle.setStance(dwe::game::battle::Stance::Guard));
  host = dwe::screens::battleViewOf(battle, names, ally);
  seen = guest.see(battle, ally, names);
  CHECK_FALSE(host.folder->noticeKey.empty());
  CHECK(seen.folder == host.folder);
  checkSameOrders(seen, host);
}

TEST_CASE("a guest picks the partner its items fly to, and its throw names it to the host") {
  const auto species = speciesTable();
  const NameKeys names{&species, nullptr, &battle_test::testCatalog()};
  Battle battle = allyBattle(BattleSystemId::Classic);
  const FighterId ally = battle.state().allies.front().id;
  GuestSide guest;
  guest.begin(beginOf(battle));
  const coop::BattleBegin& begin = *guest.mirror.running();
  const coop::BattleRole role = *guest.mirror.role();
  REQUIRE(role == coop::BattleRole::Ally);

  // Its own partner first (none picked), then the host's, around both ways.
  const std::optional<std::size_t> host =
      dwe::app::coopNextThrowTarget(begin, role, Guest, std::nullopt, 1);
  CHECK(host == std::optional<std::size_t>{dwe::game::battle::PartnerId});
  CHECK_FALSE(dwe::app::coopNextThrowTarget(begin, role, Guest, host, 1).has_value());
  CHECK(dwe::app::coopNextThrowTarget(begin, role, Guest, std::nullopt, -1) == host);

  BattleView seen = guest.see(battle, ally, names, host);
  REQUIRE(seen.allies.size() == 1);
  CHECK(seen.allies.front().throwTarget);
  seen = guest.see(battle, ally, names);
  CHECK_FALSE(seen.allies.front().throwTarget);
  const dwe::screens::ScreenSettings settings =
      dwe::screens::settingsOf(guest.see(battle, ally, names, host));
  CHECK(std::ranges::any_of(settings, [](const dwe::screens::ScreenSetting& setting) {
    const auto* on = std::get_if<bool>(&setting.value);
    return setting.name == "battle.ally.0.throw_target" && on != nullptr && *on;
  }));

  const auto thrown = guest.mirror.action(coop::BattleActionKind::ThrowItem,
                                          static_cast<std::int32_t>(*host) + 1, "meat");
  REQUIRE(thrown);
  REQUIRE(guest.room.guest(Guest).send(coop::Message{*thrown}));
  guest.room.pump();
  const auto taken = receiveOf<coop::BattleAction>(guest.room.host(Guest));
  REQUIRE(taken);
  CHECK(taken->value == static_cast<std::int32_t>(dwe::game::battle::PartnerId) + 1);
}

TEST_CASE("a guest's bag opens over its battle HUD with the focus, as the host's does") {
  const auto species = speciesTable();
  const NameKeys names{&species, nullptr, &battle_test::testCatalog()};
  Battle battle = allyBattle(BattleSystemId::Classic);
  const FighterId ally = battle.state().allies.front().id;
  GuestSide guest;
  guest.begin(beginOf(battle));
  BattleUi screen;
  BattleView hud = guest.see(battle, ally, names);
  screen.showHud(hud);
  screen.frames(3);
  REQUIRE_FALSE(screen.ui->focusedElementId(dwe::app::BattleDocument).empty());

  hud.bagOpen = true;
  dwe::app::showCoopHudBag(*screen.ui, bagOfMeat(2), hud);
  // The guest's own partner menu publishes the open bag again in the same tick
  // (PartnerMenu::update).
  dwe::screens::publish(*screen.ui, bagOfMeat(3), dwe::screens::BagRevisionSetting);
  screen.frames(3);
  // The host's next frames redraw the HUD under the bag; the focus stays on the bag.
  for (int step = 0; step < 6; ++step) {
    battle.step();
  }
  BattleView later = guest.see(battle, ally, names);
  later.bagOpen = true;
  screen.showHud(later);
  screen.frames(10);
  CHECK(screen.ui->isVisible(dwe::app::PartnerMenuDocument));
  CHECK(screen.ui->focusedElementId(dwe::app::BattleDocument).empty());
  CHECK_FALSE(screen.ui->focusedElementId(dwe::app::PartnerMenuDocument).empty());
  CHECK(std::get<bool>(screen.ui->setting(dwe::screens::BagOpenSetting)));
  // Q and E pick the partner its item flies to, and the picked ally's card lights up.
  CHECK(screen.ui->elementShown(dwe::app::PartnerMenuDocument, "throw-hint"));
  CHECK_FALSE(screen.ui->elementHasClass(dwe::app::BattleDocument, "ally-0", "throw-target"));
  BattleView picked = guest.see(battle, ally, names, dwe::game::battle::PartnerId);
  picked.bagOpen = true;
  screen.showHud(picked);
  screen.frames(2);
  CHECK(screen.ui->elementHasClass(dwe::app::BattleDocument, "ally-0", "throw-target"));
}

TEST_CASE("the host's battle closes a guest's field bag, never the bag of its battle HUD") {
  BattleUi screen;
  screen.ui->setSetting(dwe::app::PartnerMenuContextSetting, std::string{});
  screen.ui->show(dwe::app::PartnerMenuDocument, true);
  dwe::app::closeFieldBag(*screen.ui);
  CHECK_FALSE(screen.ui->isVisible(dwe::app::PartnerMenuDocument));

  screen.ui->setSetting(dwe::app::PartnerMenuContextSetting,
                        std::string{dwe::app::PartnerMenuBattleContext});
  screen.ui->show(dwe::app::PartnerMenuDocument, true);
  dwe::app::closeFieldBag(*screen.ui);
  CHECK(screen.ui->isVisible(dwe::app::PartnerMenuDocument));
}

TEST_CASE("a guest's battle HUD shows from the host's intro and leaves the end box to its own") {
  CHECK(dwe::app::coopHudPhaseOf(coop::BattlePhase::Intro) == dwe::app::IntroPhase);
  CHECK(dwe::app::coopHudPhaseOf(coop::BattlePhase::Fight) == dwe::app::FightPhase);
  CHECK(dwe::app::coopHudPhaseOf(coop::BattlePhase::Victory) == dwe::app::VictoryPhase);
  CHECK_FALSE(dwe::app::coopHudPhaseOf(coop::BattlePhase::End).has_value());
  CHECK_FALSE(dwe::app::coopHudPhaseOf(coop::BattlePhase::Closing).has_value());
}

TEST_CASE("a guest's ally rows name each partner's status as the host's rows do") {
  const auto species = speciesTable();
  const NameKeys names{&species, nullptr, &battle_test::testCatalog()};
  Battle battle = allyBattle(BattleSystemId::Classic);
  const FighterId ally = battle.state().allies.front().id;
  GuestSide guest;
  guest.begin(beginOf(battle));
  BattleView seen = guest.see(battle, ally, names);
  REQUIRE(seen.allies.size() == 1);
  CHECK(seen.allies.front().statusKey.empty());

  // The host's partner stunned: the frame names it, and the guest's row the host's key.
  seen = guest.see(battle, ally, names, std::nullopt, [](coop::BattleFrame& frame) {
    frame.fighters.at(dwe::game::battle::PartnerId).status =
        static_cast<std::uint8_t>(dwe::game::battle::StatusEffect::Stun);
  });
  CHECK(seen.allies.front().statusKey ==
        dwe::game::battle::statusTextKey(dwe::game::battle::StatusEffect::Stun));
  const coop::BattleFrame sent = dwe::app::coopBattleFrameOf(battle, ally);
  for (std::size_t index = 0; index < sent.fighters.size(); ++index) {
    CHECK(sent.fighters[index].status == static_cast<std::uint8_t>(dwe::screens::shownStatusOf(
                                             battle.state().fighters.at(index).status)));
  }
}

TEST_CASE("the Use of a guest's battle bag is a throw, even when its bag has already closed") {
  CHECK(dwe::app::coopHudThrownSlot({.name = std::string{dwe::app::BagUseCommand}, .value = 2.0}) ==
        std::optional<std::size_t>{2});
  CHECK(dwe::app::coopHudThrownSlot({.name = std::string{dwe::app::BattleThrowCommand},
                                     .value = 0.0}) == std::optional<std::size_t>{0});
  CHECK_FALSE(
      dwe::app::coopHudThrownSlot({.name = std::string{dwe::app::BagUseCommand}, .value = {}})
          .has_value());
  CHECK_FALSE(dwe::app::coopHudThrownSlot({.name = "bag_drop", .value = 1.0}).has_value());
}
