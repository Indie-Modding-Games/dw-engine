#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/game/partner/events.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/game/save/autosave.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/partner_moves.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "game/battle/tests/fixture.hpp"
#include "game/inventory/tests/fixture.hpp"

namespace items = dwe::game::inventory::testing;
using dwe::assets::DigimonRow;
using dwe::assets::DigimonTable;
using dwe::game::battle::BattleKind;
using dwe::game::battle::BattleResult;
using dwe::game::battle::Combatant;
using dwe::game::battle::Outcome;
using dwe::game::partner::Condition;
using dwe::game::partner::EventKind;
using dwe::game::partner::PartnerState;
using dwe::game::partner::Stat;
using dwe::game::save::AutosaveReason;
using dwe::game::save::MemorySaveStorage;
using dwe::session::BattleAdapter;
using dwe::session::BattleAdapterDesc;
using dwe::session::BattleRequest;
using dwe::session::EvolutionAdapter;
using dwe::session::GameSession;
using dwe::session::InventoryAdapter;
using dwe::session::InventoryAdapterDesc;
using dwe::session::PartnerMoves;
using dwe::session::SessionSystem;
using dwe::session::testing::errorOf;
using dwe::session::testing::newGameWith;
using dwe::session::testing::TestPacks;

namespace {

// The made-up new game partner's move list: techniques at 0 to 3, a finisher at 4 and 6, and a gap.
constexpr std::int32_t FinisherPosition = 6;
constexpr std::int32_t ShotPosition = 1;
constexpr std::int32_t Bits = 120;
constexpr std::int32_t FullHp = 900;

DigimonRow madeUpRow(std::string id, std::int32_t index, std::vector<std::string> moves) {
  DigimonRow row;
  row.id = std::move(id);
  row.index = index;
  row.name = row.id;
  row.radius = 100;
  row.type = 2;
  row.level = 3;
  row.special = {1, std::nullopt, std::nullopt};
  for (std::size_t slot = 0; slot < moves.size(); ++slot) {
    if (!moves[slot].empty()) {
      row.moves.at(slot) = moves[slot];
    }
  }
  return row;
}

// The fixture forms' new game partner with made-up moves, and a foe.
DigimonTable madeUpDigimon() {
  DigimonTable table;
  table.digimon.push_back(madeUpRow("agum", 8,
                                    {"punch", "shot", "dynamite_kick", "war_cry", "big_finish", "",
                                     "big_finish", "", "poison_bite"}));
  table.digimon.push_back(madeUpRow("foe", 90, {"punch"}));
  return table;
}

struct Adapters {
  BattleAdapter* battle = nullptr;
  InventoryAdapter* inventory = nullptr;
};

std::unique_ptr<GameSession> sessionWithBattle(const TestPacks& packs, Adapters& adapters,
                                               MemorySaveStorage* storage = nullptr) {
  auto inventory =
      std::make_unique<InventoryAdapter>(InventoryAdapterDesc{.files = &packs.files(),
                                                              .items = items::fixtureItems(),
                                                              .effects = items::fixtureEffects(),
                                                              .evolution = nullptr});
  auto battle =
      std::make_unique<BattleAdapter>(BattleAdapterDesc{.files = &packs.files(),
                                                        .digimon = madeUpDigimon(),
                                                        .moves = battle_test::testMoveTable(),
                                                        .inventory = inventory.get()});
  adapters = {.battle = battle.get(), .inventory = inventory.get()};
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(inventory));
  systems.push_back(std::move(battle));
  return newGameWith(packs, std::move(systems), storage);
}

// A session with the evolution adapter on `fixture`'s forms counting the battle adapter's learned
// moves.
std::unique_ptr<GameSession>
sessionWithEvolutionAndBattle(const TestPacks& packs,
                              const dwe::game::evolution::testing::TableFixture& fixture,
                              Adapters& adapters) {
  auto evolution = dwe::session::testing::evolutionAdapter(packs, fixture);
  auto inventory =
      std::make_unique<InventoryAdapter>(InventoryAdapterDesc{.files = &packs.files(),
                                                              .items = items::fixtureItems(),
                                                              .effects = items::fixtureEffects(),
                                                              .evolution = evolution.get()});
  auto battle =
      std::make_unique<BattleAdapter>(BattleAdapterDesc{.files = &packs.files(),
                                                        .digimon = madeUpDigimon(),
                                                        .moves = battle_test::testMoveTable(),
                                                        .inventory = inventory.get()});
  evolution->setMoveSource(*battle);
  adapters = {.battle = battle.get(), .inventory = inventory.get()};
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(evolution));
  systems.push_back(std::move(inventory));
  systems.push_back(std::move(battle));
  return newGameWith(packs, std::move(systems));
}

template <typename Change>
void changePartner(GameSession& session, Change change) {
  PartnerState state = session.partner().state();
  change(state);
  REQUIRE(session.partner().restore(state).has_value());
}

// Plays on past the save rules' coalescing seconds and says whether an autosave for `reason` was
// written.
bool autosavedFor(GameSession& session, AutosaveReason reason) {
  constexpr int CoalescingTicks = 3;
  session.clock().setSpeed(1.0);
  for (int tick = 0; tick < CoalescingTicks; ++tick) {
    session.tick(1.0);
    if (const auto autosave = session.takeAutosaved()) {
      return std::ranges::find(autosave->reasons, reason) != autosave->reasons.end();
    }
  }
  return false;
}

BattleResult victory() {
  BattleResult result;
  result.outcome = Outcome::Victory;
  result.partnerHp = 40;
  result.partnerMp = 30;
  result.statGains = {10, 0, 3, 0, 0, 2};
  result.bits = Bits;
  result.drops = {{.item = "tonic", .species = "foe", .position = {.x = 100, .z = 600}}};
  result.learnedMoves = {"dynamite_kick", "punch"};
  result.care = {.happiness = 2, .discipline = 0, .tiredness = 5, .battles = 1};
  result.foodChange = -15;
  result.elapsedMinutes = 20;
  return result;
}

BattleRequest duelWithFoe() {
  Combatant foe =
      battle_test::fighterWith("foe", battle_test::evenStats(50, 1), {"punch"}, {.x = 0, .z = 600});
  return {.enemies = {std::move(foe)},
          .scripted = false,
          .tamerless = false,
          .map = "test_map",
          .tamer = {.x = 0, .z = -800},
          .partner = {.x = 0, .z = -600},
          .kind = BattleKind::Normal,
          .recruitOnVictory = "foe"};
}

} // namespace

TEST_CASE(
    "a new game's partner learns and equips its first technique and equips the last finisher of its list") {
  const TestPacks packs;
  Adapters adapters;
  auto session = sessionWithBattle(packs, adapters);
  const PartnerMoves& moves = adapters.battle->moves();
  CHECK(moves.learned == std::vector<std::string>{"punch"});
  CHECK(moves.equipped.at(0) == 0);
  CHECK_FALSE(moves.equipped.at(1).has_value());
  CHECK_FALSE(moves.equipped.at(2).has_value());
  CHECK(moves.equipped.at(dwe::game::battle::FinisherSlot) == FinisherPosition);
}

TEST_CASE(
    "the evolution requirements count the learned moves below the evolution section's limit") {
  // evolution.c: getNumMasteredMoves; the rookie's check hour is check_hours.rookie (72).
  constexpr std::int32_t RookieHours = 72;
  dwe::game::evolution::testing::TableFixture fixture;
  fixture.setPath("agum", {"species_d"});
  // A champion target asks for a stat (the rookie's pick averages them), and its bonus is two
  // learned moves.
  fixture.requirement("species_d").offense = 1;
  fixture.requirement("species_d").techs = 2;
  // The session composes one move table for both systems: the evolution's names the battle's moves
  // alike.
  fixture.moves.at(0).id = "punch";
  fixture.moves.at(46).id = "dynamite_kick";
  fixture.moves.at(119).id = "move_121";
  // The new game's technique and one more, but the second is past the 64 moves the original counts.
  const auto speciesAfterLearning = [&](const std::string& move) {
    const TestPacks packs;
    Adapters adapters;
    auto session = sessionWithEvolutionAndBattle(packs, fixture, adapters);
    BattleResult result = victory();
    result.learnedMoves = {move};
    REQUIRE(adapters.battle->finish(*session, result).has_value());
    REQUIRE(dwe::session::knows(adapters.battle->moves(), move));
    changePartner(*session, [](PartnerState& state) { state.evolution.hours = RookieHours - 1; });
    session->clock().setSpeed(dwe::session::testing::GameHourPerSecond);
    session->tick(1.0);
    return session->partner().state().species;
  };
  CHECK(speciesAfterLearning("move_121") == "agum");
  CHECK(speciesAfterLearning("dynamite_kick") == "species_d");
}

TEST_CASE("the partner fights with its stats, HP, MP and the moves at its equipped positions") {
  const TestPacks packs;
  Adapters adapters;
  auto session = sessionWithBattle(packs, adapters);
  changePartner(*session, [](PartnerState& state) { state.stats.currentHp = 77; });
  const auto fighter = adapters.battle->partnerCombatant(*session);
  REQUIRE_MESSAGE(fighter.has_value(), errorOf(fighter));
  const auto& base = session->partner().state().stats.base;
  CHECK(fighter->species == "agum");
  CHECK(fighter->stats.maxHp == base[Stat::Hp]);
  CHECK(fighter->stats.brain == base[Stat::Brain]);
  CHECK(fighter->hp == 77);
  CHECK(fighter->moves.at(0) == "punch");
  CHECK_FALSE(fighter->moves.at(1).has_value());
  CHECK(fighter->moves.at(dwe::game::battle::FinisherSlot) == "big_finish");
}

TEST_CASE("only learned techniques are equipped, at positions the species has a move at") {
  const TestPacks packs;
  Adapters adapters;
  auto session = sessionWithBattle(packs, adapters);
  CHECK_FALSE(adapters.battle->equip(*session, 1, ShotPosition).has_value());
  CHECK_FALSE(adapters.battle->equip(*session, 1, 5).has_value());
  CHECK_FALSE(adapters.battle->equip(*session, 4, 0).has_value());
  REQUIRE(adapters.battle->finish(*session, victory()).has_value());
  REQUIRE(adapters.battle->equip(*session, 1, 2).has_value());
  REQUIRE(adapters.battle->equip(*session, 2, std::nullopt).has_value());
  CHECK(adapters.battle->moves().equipped.at(1) == 2);
}

TEST_CASE(
    "a won battle gives the partner its HP, raises, care, minutes, moves and bits, leaves the drops on the ground, and autosaves") {
  const TestPacks packs;
  MemorySaveStorage storage;
  Adapters adapters;
  auto session = sessionWithBattle(packs, adapters, &storage);
  const PartnerState before = session->partner().state();
  const std::int32_t minute = session->clock().time().minute;
  const std::int32_t money = adapters.inventory->inventory().money();
  const std::int32_t tonics =
      adapters.inventory->inventory().countOf(items::handleOf(items::Tonic));
  BattleResult result = victory();
  result.recruitJoined = "foe";
  const auto aftermath = adapters.battle->finish(*session, result);
  REQUIRE_MESSAGE(aftermath.has_value(), errorOf(aftermath));
  const PartnerState& after = session->partner().state();
  CHECK(after.stats.currentHp == 40);
  CHECK(after.stats.currentMp == 30);
  CHECK(after.stats.base[Stat::Hp] == before.stats.base[Stat::Hp] + 10);
  CHECK(after.stats.base[Stat::Offense] == before.stats.base[Stat::Offense] + 3);
  CHECK(aftermath->statGains[Stat::Brain] == 2);
  CHECK(after.mood.happiness == before.mood.happiness + 2);
  CHECK(after.mood.tiredness == before.mood.tiredness + 5);
  CHECK(after.evolution.battles == before.evolution.battles + 1);
  CHECK(session->clock().time().minute == (minute + 20) % 60);
  CHECK(aftermath->learnedMoves == std::vector<std::string>{"dynamite_kick"});
  CHECK(dwe::session::knows(adapters.battle->moves(), "dynamite_kick"));
  CHECK(adapters.inventory->inventory().money() == money + Bits);
  CHECK(adapters.inventory->inventory().countOf(items::handleOf(items::Tonic)) == tonics);
  CHECK(aftermath->drops == result.drops);
  CHECK(aftermath->recruitJoined == "foe");
  CHECK(autosavedFor(*session, AutosaveReason::BattleEnd));
}

TEST_CASE("a lost battle costs a life and items from the bag, and its hour costs lifetime") {
  const TestPacks packs;
  Adapters adapters;
  auto session = sessionWithBattle(packs, adapters);
  for (const std::string_view item :
       {items::Tonic, items::Ether, items::Bandage, items::Bun, items::Steak}) {
    CHECK(adapters.inventory->inventory().add(items::handleOf(item), 1).taken());
  }
  const PartnerState before = session->partner().state();
  BattleResult result;
  result.outcome = Outcome::Defeat;
  result.care = {.happiness = -30, .discipline = -20, .tiredness = 0, .battles = 0};
  result.livesLost = 1;
  result.elapsedMinutes = 20;
  result.hourCrossed = true;
  result.lifetimeHoursLost = 1;
  const auto aftermath = adapters.battle->finish(*session, result);
  REQUIRE_MESSAGE(aftermath.has_value(), errorOf(aftermath));
  const PartnerState& after = session->partner().state();
  CHECK(after.life.lives == before.life.lives - 1);
  CHECK(after.life.remainingLifetime == before.life.remainingLifetime - 1);
  CHECK(after.evolution.battles == before.evolution.battles);
  for (const auto& lost : aftermath->itemsLost) {
    CHECK(adapters.inventory->inventory().countOf(lost.item) == 0);
  }
}

TEST_CASE("a tournament's battle asks for the tournament's autosave besides the battle's end") {
  const TestPacks packs;
  MemorySaveStorage storage;
  Adapters adapters;
  auto session = sessionWithBattle(packs, adapters, &storage);
  BattleResult result = victory();
  result.kind = BattleKind::Tournament;
  REQUIRE(adapters.battle->finish(*session, result).has_value());
  CHECK(autosavedFor(*session, AutosaveReason::TournamentResult));
}

TEST_CASE("a recruit that joins asks for its autosave") {
  const TestPacks packs;
  MemorySaveStorage storage;
  Adapters adapters;
  auto session = sessionWithBattle(packs, adapters, &storage);
  BattleResult result = victory();
  result.recruitJoined = "foe";
  REQUIRE(adapters.battle->finish(*session, result).has_value());
  CHECK(autosavedFor(*session, AutosaveReason::RecruitJoin));
}

TEST_CASE(
    "a battle started by the session runs to its end and its result goes back to the partner") {
  const TestPacks packs;
  Adapters adapters;
  auto session = sessionWithBattle(packs, adapters);
  changePartner(*session, [](PartnerState& state) {
    state.stats.base[Stat::Hp] = FullHp;
    state.stats.currentHp = FullHp;
    state.stats.base[Stat::Offense] = 500;
    state.mood.discipline = 100;
  });
  auto battle = adapters.battle->start(*session, duelWithFoe());
  REQUIRE_MESSAGE(battle.has_value(), errorOf(battle));
  CHECK(battle->state().partner().position == dwe::game::battle::Point{.x = 0, .z = -600});
  battle_test::runToEnd(*battle);
  REQUIRE(battle->result().has_value());
  const BattleResult result = battle->result().value_or(BattleResult{});
  CHECK(result.outcome == Outcome::Victory);
  CHECK(result.recruitJoined == "foe");
  const auto aftermath = adapters.battle->finish(*session, result);
  REQUIRE_MESSAGE(aftermath.has_value(), errorOf(aftermath));
  CHECK(session->partner().state().stats.currentHp == result.partnerHp);
  CHECK(session->partner().state().evolution.battles == 1);
}

TEST_CASE(
    "an item thrown in battle is consumed at once and lands with the throw; field items stay in the bag") {
  using dwe::game::inventory::UseOutcome;
  const TestPacks packs;
  Adapters adapters;
  auto session = sessionWithBattle(packs, adapters);
  changePartner(*session, [](PartnerState& state) {
    state.stats.base[Stat::Hp] = FullHp;
    state.stats.currentHp = FullHp;
  });
  dwe::game::inventory::Inventory& bag = adapters.inventory->inventory();
  bag.add(items::handleOf(items::Tonic), 2);
  bag.add(items::handleOf(items::Bandage), 1);
  auto battle = adapters.battle->start(*session, duelWithFoe());
  REQUIRE_MESSAGE(battle.has_value(), errorOf(battle));

  // inventory.c: selectOptionMenu: in battle only item_color 1 and 2.
  const auto bandage = adapters.battle->throwItem(*battle, items::handleOf(items::Bandage));
  REQUIRE_MESSAGE(bandage.has_value(), errorOf(bandage));
  CHECK(bandage->outcome == UseOutcome::NotUsableHere);
  CHECK(bag.countOf(items::handleOf(items::Bandage)) == 1);
  CHECK_FALSE(battle->itemInFlight());

  // main.c: startThrowingItem: removeItem when thrown; one item on its way at a time.
  const auto tonic = adapters.battle->throwItem(*battle, items::handleOf(items::Tonic));
  REQUIRE_MESSAGE(tonic.has_value(), errorOf(tonic));
  CHECK(tonic->outcome == UseOutcome::Used);
  CHECK(bag.countOf(items::handleOf(items::Tonic)) == 1);
  CHECK(battle->itemInFlight());
  CHECK_FALSE(adapters.battle->throwItem(*battle, items::handleOf(items::Tonic)).has_value());
  CHECK(bag.countOf(items::handleOf(items::Tonic)) == 1);
  bool landed = false;
  while (battle->itemInFlight() && !battle->finished()) {
    landed =
        battle_test::hasEvent(battle->step(), dwe::game::battle::EventKind::ItemLanded) || landed;
  }
  CHECK(landed);
}

TEST_CASE("a reborn partner equips one move again and keeps the learned ones") {
  const TestPacks packs;
  Adapters adapters;
  auto session = sessionWithBattle(packs, adapters);
  REQUIRE(adapters.battle->finish(*session, victory()).has_value());
  adapters.battle->routePartnerEvent(*session, {.kind = EventKind::Reborn, .value = 0});
  const PartnerMoves& moves = adapters.battle->moves();
  CHECK(moves.equipped.at(0) == 0);
  CHECK_FALSE(moves.equipped.at(dwe::game::battle::FinisherSlot).has_value());
  CHECK(dwe::session::knows(moves, "dynamite_kick"));
}

TEST_CASE("an evolution teaches the new form's move and sets the four places again") {
  // evl.c: EVL_applyEvolution: learnMove, then moves[0..3].
  const TestPacks packs;
  Adapters adapters;
  auto session = sessionWithBattle(packs, adapters);
  const std::array<std::optional<dwe::session::MoveSlot>, dwe::game::battle::EquippedSlots>
      equipped{2, std::nullopt, std::nullopt, 5};
  adapters.battle->evolveMoves(std::string{"dynamite_kick"}, equipped);
  const PartnerMoves& moves = adapters.battle->moves();
  CHECK(dwe::session::knows(moves, "dynamite_kick"));
  CHECK(dwe::session::knows(moves, "punch"));
  CHECK(moves.equipped == equipped);

  // A form that learns nothing (an in-training one) only has its places set.
  const std::vector<std::string> learned = moves.learned;
  adapters.battle->evolveMoves(std::nullopt, {});
  CHECK(adapters.battle->moves().learned == learned);
  CHECK_FALSE(adapters.battle->moves().equipped.at(0).has_value());
}

TEST_CASE("the partner's moves go in the save and come back") {
  const TestPacks packs;
  Adapters saving;
  auto saved = sessionWithBattle(packs, saving);
  REQUIRE(saving.battle->finish(*saved, victory()).has_value());
  REQUIRE(saving.battle->equip(*saved, 2, 2).has_value());
  auto file = saved->saveFile();
  REQUIRE_MESSAGE(file.has_value(), errorOf(file));
  REQUIRE(file->sections.contains("moves"));

  Adapters loading;
  auto loaded = sessionWithBattle(packs, loading);
  const auto read = loaded->loadFile(*file);
  REQUIRE_MESSAGE(read.has_value(), errorOf(read));
  CHECK(loading.battle->moves() == saving.battle->moves());
}

TEST_CASE("the moves' save form is checked: four places, positions in the list") {
  auto node = dwe::core::parseYaml("learned: [punch]\nequipped: [0, null, null]\n");
  REQUIRE(node.has_value());
  CHECK_FALSE(dwe::session::partnerMovesFromYaml(*node).has_value());
  node = dwe::core::parseYaml("learned: [shot, punch, shot]\nequipped: [0, null, 16, null]\n");
  REQUIRE(node.has_value());
  CHECK_FALSE(dwe::session::partnerMovesFromYaml(*node).has_value());
  node = dwe::core::parseYaml("learned: [shot, punch, shot]\nequipped: [0, null, 15, null]\n");
  REQUIRE(node.has_value());
  const auto moves = dwe::session::partnerMovesFromYaml(*node);
  REQUIRE_MESSAGE(moves.has_value(), errorOf(moves));
  CHECK(moves->learned == std::vector<std::string>{"punch", "shot"});
}

TEST_CASE("the battle adapter registers the battle and partner_moves sections' checks") {
  const TestPacks packs;
  Adapters adapters;
  auto session = sessionWithBattle(packs, adapters);
  CHECK_FALSE(session->rules().setValue("partner_moves", "new_game_slot", "16").has_value());
  CHECK_FALSE(
      session->rules().setValue("battle", "consequences.elapsed_minutes", "soon").has_value());
}

// Runs only when DWE_CONTENT names a content folder (scripts/dev.sh mounts it).
TEST_CASE(
    "the user's disc, extracted: the first partner starts with its first technique and its finisher") {
  const std::optional<std::filesystem::path> mountedFolder = dwe::test_support::userContent();
  if (!mountedFolder) {
    MESSAGE("DWE_CONTENT is not set: skipping the partner's moves on extracted content");
    return;
  }
  const std::filesystem::path& folder = *mountedFolder;
  const dwe::assets::DiskFileSystem disk;
  const std::array<dwe::assets::PackLocation, 2> locations{
      dwe::assets::PackLocation{.fileSystem = &disk,
                                .root = folder / "base",
                                .fillsGaps = false,
                                .origin = dwe::assets::PackOrigin::Engine},
      dwe::assets::PackLocation{.fileSystem = &disk,
                                .root = DWE_GAME_PACK,
                                .fillsGaps = false,
                                .origin = dwe::assets::PackOrigin::Engine}};
  auto files = dwe::assets::VirtualFileSystem::mount(locations);
  REQUIRE_MESSAGE(files.has_value(), errorOf(files));
  auto battle = std::make_unique<BattleAdapter>(BattleAdapterDesc{
      .files = &*files, .digimon = std::nullopt, .moves = std::nullopt, .inventory = nullptr});
  BattleAdapter* adapter = battle.get();
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(battle));
  auto session = GameSession::create({.files = &*files,
                                      .rulesetId = "original",
                                      .rules = std::nullopt,
                                      .species = nullptr,
                                      .seed = 7,
                                      .storage = nullptr,
                                      .gameVersion = "test",
                                      .wallClock = [] { return dwe::session::testing::SaveDate; },
                                      .systems = std::move(systems)});
  REQUIRE_MESSAGE(session.has_value(), errorOf(session));
  const auto started = (*session)->startNewGame();
  REQUIRE_MESSAGE(started.has_value(), errorOf(started));
  const auto fighter = adapter->partnerCombatant(**session);
  REQUIRE_MESSAGE(fighter.has_value(), errorOf(fighter));
  // partner_impl.c: initializePartner(3): moves[0] = 0x2E with learnMove(2); the finisher is the
  // list's last.
  CHECK(adapter->moves().learned == std::vector<std::string>{"spit_fire"});
  CHECK(fighter->moves.at(0) == "spit_fire");
  CHECK(fighter->moves.at(dwe::game::battle::FinisherSlot) == "pepper_breath");
}
