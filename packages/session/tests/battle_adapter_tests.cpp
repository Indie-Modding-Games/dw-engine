#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/game/partner/events.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/game/save/autosave.hpp>
#include <dwe/session/battle_adapter.hpp>
#include <dwe/session/battle_registry.hpp>
#include <dwe/session/evolution_adapter.hpp>
#include <dwe/session/game_clear.hpp>
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
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "adapter_fixture.hpp"
#include "packages/game/battle/tests/fixture.hpp"
#include "packages/game/inventory/tests/fixture.hpp"

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
  // An enemy's copy of the partner's form: a model, the same name and no move of its own.
  DigimonRow copy = madeUpRow("agum_copy", 91, {});
  copy.name = "agum";
  copy.boneCount = 20;
  table.digimon.push_back(copy);
  return table;
}

struct Adapters {
  BattleAdapter* battle = nullptr;
  InventoryAdapter* inventory = nullptr;
};

std::unique_ptr<GameSession> sessionWithBattle(const TestPacks& packs, Adapters& adapters,
                                               MemorySaveStorage* storage = nullptr,
                                               std::string_view rulesetId = "original") {
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
  return newGameWith(packs, std::move(systems), storage, 7, rulesetId);
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
          .timeLimit = std::nullopt,
          .recruitOnVictory = "foe",
          .matchMusic = std::nullopt};
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

TEST_CASE("a partner of a species that lists no move fights with its namesake's moves") {
  const TestPacks packs;
  Adapters adapters;
  auto session = sessionWithBattle(packs, adapters);
  const DigimonRow* copy = adapters.battle->partnerRow("agum_copy");
  REQUIRE(copy != nullptr);
  CHECK(copy->id == "agum_copy");
  CHECK(copy->moves == adapters.battle->partnerRow("agum")->moves);
  // The table itself keeps the enemy's row as it is.
  CHECK_FALSE(dwe::assets::listsMoves(*adapters.battle->digimonTable()->find("agum_copy")));
}

TEST_CASE("a new game's partner of a species with its own position learns and equips that one") {
  // partner_impl.c: initializePartner: the second starting species equips and learns position 2.
  const TestPacks packs;
  Adapters adapters;
  auto session = sessionWithBattle(packs, adapters);
  REQUIRE(session->rules().setValue("partner_moves", "new_game_species_slots", "{agum: 2}"));
  // The session reads changed rules on its next tick.
  session->tick(0.0);
  REQUIRE(session->startNewGame());
  const PartnerMoves& moves = adapters.battle->moves();
  CHECK(moves.learned == std::vector<std::string>{"dynamite_kick"});
  CHECK(moves.equipped.at(0) == 2);
  CHECK_FALSE(moves.equipped.at(1).has_value());
  CHECK_FALSE(moves.equipped.at(2).has_value());
  CHECK(moves.equipped.at(dwe::game::battle::FinisherSlot) == FinisherPosition);
  // Another species keeps new_game_slot.
  REQUIRE(session->rules().setValue("partner_moves", "new_game_species_slots", "{foe: 2}"));
  session->tick(0.0);
  REQUIRE(session->startNewGame());
  CHECK(adapters.battle->moves().learned == std::vector<std::string>{"punch"});
  CHECK(adapters.battle->moves().equipped.at(0) == 0);
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

TEST_CASE("a ruleset's species patches give the partner its own type and move values") {
  // A ruleset with the hidden content's partner fields for the made-up partner.
  const TestPacks packs{
      {"rules/patched/ruleset.yaml",
       "id: patched\nname: patched\nversion: 0.1.0\ndescription: test\nextends: original\n"
       "sections:\n  - species_extra\n"},
      {"rules/patched/species_extra.yaml",
       "species:\n  agum:\n    partner_type: 3\n    partner_moves:\n"
       "      punch: { power: 240, iframes: 1 }\n"}};
  Adapters adapters;
  auto session = sessionWithBattle(packs, adapters, nullptr, "patched");
  const auto fighter = adapters.battle->partnerCombatant(*session);
  REQUIRE_MESSAGE(fighter.has_value(), errorOf(fighter));
  CHECK(fighter->type == dwe::game::battle::DigimonType::Virus);
  const auto variant = fighter->moveVariants.find("punch");
  REQUIRE(variant != fighter->moveVariants.end());
  const auto& catalog = adapters.battle->catalog();
  CHECK(catalog.at(variant->second).power == 240);
  CHECK(catalog.at(variant->second).iframes == 1);
  // Everyone else's punch is the table's.
  const auto table = catalog.find("punch");
  REQUIRE(table.has_value());
  CHECK(variant->second != *table);
  CHECK(catalog.at(*table).power == 30);
}

TEST_CASE("a species drawn at another size fights with its body at that size") {
  const TestPacks packs{
      {"rules/patched/ruleset.yaml",
       "id: patched\nname: patched\nversion: 0.1.0\ndescription: test\nextends: original\n"
       "sections:\n  - species_extra\n"},
      {"rules/patched/species_extra.yaml", "species:\n  agum:\n    field_scale: 0.5\n"}};
  Adapters usualAdapters;
  auto usualSession = sessionWithBattle(packs, usualAdapters);
  const auto usual = usualAdapters.battle->partnerCombatant(*usualSession);
  REQUIRE_MESSAGE(usual.has_value(), errorOf(usual));
  Adapters adapters;
  auto session = sessionWithBattle(packs, adapters, nullptr, "patched");
  const auto halved = adapters.battle->partnerCombatant(*session);
  REQUIRE_MESSAGE(halved.has_value(), errorOf(halved));
  REQUIRE(usual->radius > 0);
  CHECK(halved->radius == (usual->radius + 1) / 2);
  CHECK(halved->height == (usual->height + 1) / 2);
}

TEST_CASE("the partner fights with its own charge mode, 0 unless something changed it") {
  // battle_setup.c: BTL_initializeCombat keeps PARTNER_ENTITY's chargeMode, 0 in every measured
  // state of the original (new game, the forest fight, later saves).
  const TestPacks packs;
  Adapters adapters;
  auto session = sessionWithBattle(packs, adapters);
  const auto usual = adapters.battle->partnerCombatant(*session);
  REQUIRE_MESSAGE(usual.has_value(), errorOf(usual));
  CHECK(usual->chargeMode == dwe::game::battle::ChargeMode::Aggressive);
  changePartner(*session, [](PartnerState& state) { state.stats.chargeMode = 2; });
  const auto cautious = adapters.battle->partnerCombatant(*session);
  REQUIRE_MESSAGE(cautious.has_value(), errorOf(cautious));
  CHECK(cautious->chargeMode == dwe::game::battle::ChargeMode::Cautious);
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

TEST_CASE("a story battle the session starts as scripted offers no Run and refuses a flee") {
  // btl.c: handleBattleStart: FLEE_DISABLED[0] (trigger 1); battle_tick.c: handleCommands skips Run
  // on the strip, so the fight ends only by winning or losing.
  using dwe::game::battle::Command;
  const TestPacks packs;
  Adapters adapters;
  auto session = sessionWithBattle(packs, adapters);
  auto free = adapters.battle->start(*session, duelWithFoe());
  REQUIRE_MESSAGE(free.has_value(), errorOf(free));
  CHECK(std::ranges::find(free->menu(), Command::Flee) != free->menu().end());

  BattleRequest request = duelWithFoe();
  request.scripted = true;
  auto fixed = adapters.battle->start(*session, std::move(request));
  REQUIRE_MESSAGE(fixed.has_value(), errorOf(fixed));
  CHECK(std::ranges::find(fixed->menu(), Command::Flee) == fixed->menu().end());
  CHECK_FALSE(fixed->command(Command::Flee).has_value());
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

TEST_CASE("script_common.c lostAllLives: (10 - trainer level) * 2 percent of the moves, rounded") {
  // Trainer level 1, 20 moves: 18 % of 20 is 3.6, rounded to 4.
  CHECK(dwe::session::movesToForget(20, 1) == 4);
  // 18 % of 2 is 0.36: none.
  CHECK(dwe::session::movesToForget(2, 1) == 0);
  // 18 % of 3 is 0.54: one.
  CHECK(dwe::session::movesToForget(3, 1) == 1);
  CHECK(dwe::session::movesToForget(30, 10) == 0);
  CHECK(dwe::session::movesToForget(0, 0) == 0);
}

TEST_CASE("utils2.c unlearnMove: the 16-bit mask also clears the upper half of the move's word") {
  using dwe::session::movesClearedBy;
  const auto upperHalf = [](std::int32_t word) {
    std::vector<std::int32_t> moves;
    for (std::int32_t bit = 16; bit < 32; ++bit) {
      moves.push_back((word * 32) + bit);
    }
    return moves;
  };
  std::vector<std::int32_t> expected{1};
  std::ranges::copy(upperHalf(0), std::back_inserter(expected));
  CHECK(movesClearedBy(1) == expected);
  // A move of the upper half: its own bit is among them.
  CHECK(movesClearedBy(20) == upperHalf(0));
  // 0x30 clears 0x2C's bit, and itself only through the upper half.
  expected = {0x2C};
  std::ranges::copy(upperHalf(1), std::back_inserter(expected));
  CHECK(movesClearedBy(0x30) == expected);
  CHECK(movesClearedBy(0x37) == upperHalf(1));
  // Without the quirk (partner_moves.quirks.forget_mask_16_bits false) only the move goes.
  CHECK(movesClearedBy(1, false) == std::vector<std::int32_t>{1});
  CHECK(movesClearedBy(0x30, false) == std::vector<std::int32_t>{0x30});
  CHECK(movesClearedBy(0x37, false) == std::vector<std::int32_t>{0x37});
}

TEST_CASE("the original ruleset keeps unlearnMove's 16-bit mask, and another can fix it") {
  const TestPacks packs;
  Adapters adapters;
  auto session = sessionWithBattle(packs, adapters);
  REQUIRE(adapters.battle->finish(*session, victory()).has_value());
  REQUIRE(adapters.battle->learnMoveNumber(1).has_value());
  REQUIRE(adapters.battle->learnMoveNumber(22).has_value());
  REQUIRE(session->rules()
              .setValue("partner_moves", "quirks.forget_mask_16_bits", "false")
              .has_value());
  // The session reads changed rules on its next tick.
  session->tick(0.0);
  const std::vector<std::string> forgotten = adapters.battle->unlearnMoveNumber(*session, 1);
  CHECK(forgotten == std::vector<std::string>{"shot"});
  CHECK(dwe::session::knows(adapters.battle->moves(), "aqua_magic"));
}

TEST_CASE("utils2.c unlearnMove: the partner forgets the move, its place empties, others go too") {
  const TestPacks packs;
  Adapters adapters;
  auto session = sessionWithBattle(packs, adapters);
  REQUIRE(adapters.battle->finish(*session, victory()).has_value());
  REQUIRE(adapters.battle->learnMoveNumber(1).has_value());
  REQUIRE(adapters.battle->learnMoveNumber(22).has_value());
  REQUIRE(adapters.battle->equip(*session, 1, ShotPosition).has_value());
  const std::vector<std::int32_t> known = adapters.battle->knownMoveNumbers();
  CHECK(std::ranges::contains(known, 1));
  CHECK(std::ranges::contains(known, 22));
  CHECK(std::ranges::contains(known, 46));
  const std::vector<std::string> forgotten = adapters.battle->unlearnMoveNumber(*session, 1);
  // shot's bit, and aqua_magic (22) through the mask's upper half.
  CHECK(std::ranges::contains(forgotten, "shot"));
  CHECK(std::ranges::contains(forgotten, "aqua_magic"));
  const PartnerMoves& moves = adapters.battle->moves();
  CHECK_FALSE(moves.equipped.at(1).has_value());
  CHECK(dwe::session::knows(moves, "dynamite_kick"));
  CHECK_FALSE(dwe::session::knows(moves, "shot"));
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

TEST_CASE("the ending's save finishes the game and gives the registry's last record") {
  // main_menu.c: gameClearSave (MAIN_STATE 2) and awardMachinedramonData: record 39 takes the
  // partner's name, stats and discipline as another species with three of its techniques.
  const TestPacks packs;
  Adapters adapters;
  const auto session = sessionWithBattle(packs, adapters);
  dwe::session::BattleRegistry registry;
  const auto partner = dwe::session::registeredPartnerOf(*session, *adapters.battle);
  REQUIRE_MESSAGE(partner.has_value(), errorOf(partner));
  const dwe::session::EndingGift gift{
      .slot = 39, .species = "foe", .techniques = {"punch", "shot"}};
  REQUIRE_FALSE(session->gameCleared());
  const auto cleared = dwe::session::clearGame(*session, registry, *adapters.battle, gift, "fila");
  REQUIRE_MESSAGE(cleared.has_value(), errorOf(cleared));
  CHECK(session->gameCleared());
  REQUIRE(registry.at(39).has_value());
  const dwe::session::RegisteredPartner& record = *registry.at(39);
  CHECK(record.species == "foe");
  CHECK(record.name == partner->name);
  CHECK(record.hp == partner->hp);
  CHECK(record.brains == partner->brains);
  CHECK(record.discipline == partner->discipline);
  CHECK(record.techniques.at(0) == "punch");
  CHECK(record.techniques.at(1) == "shot");
  CHECK_FALSE(record.techniques.at(2).has_value());
  // Without a gift the game is finished all the same and nothing is registered.
  dwe::session::BattleRegistry empty;
  REQUIRE(dwe::session::clearGame(*session, empty, *adapters.battle, std::nullopt, std::nullopt));
  CHECK(session->gameCleared());
  CHECK_FALSE(empty.at(39).has_value());
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
  CHECK_FALSE(session->rules()
                  .setValue("partner_moves", "new_game_species_slots", "{agum: 16}")
                  .has_value());
  CHECK_FALSE(
      session->rules().setValue("partner_moves", "new_game_species_slots", "[2]").has_value());
  CHECK_FALSE(
      session->rules().setValue("battle", "consequences.elapsed_minutes", "soon").has_value());
}

namespace {

// A session on the user's extracted content and the game pack, with only the battle adapter.
struct ExtractedSession {
  dwe::assets::DiskFileSystem disk;
  std::optional<dwe::assets::VirtualFileSystem> files;
  BattleAdapter* battle = nullptr;
  std::unique_ptr<GameSession> session;
};

// Nothing when DWE_CONTENT is not set (scripts/dev.sh mounts it).
std::unique_ptr<ExtractedSession> extractedSession() {
  const std::optional<std::filesystem::path> mountedFolder = dwe::test_support::userContent();
  if (!mountedFolder) {
    return nullptr;
  }
  auto made = std::make_unique<ExtractedSession>();
  const std::array<dwe::assets::PackLocation, 2> locations{
      dwe::assets::PackLocation{.fileSystem = &made->disk,
                                .root = *mountedFolder / "base",
                                .fillsGaps = false,
                                .origin = dwe::assets::PackOrigin::Engine},
      dwe::assets::PackLocation{.fileSystem = &made->disk,
                                .root = DWE_GAME_PACK,
                                .fillsGaps = false,
                                .origin = dwe::assets::PackOrigin::Engine}};
  auto files = dwe::assets::VirtualFileSystem::mount(locations);
  REQUIRE_MESSAGE(files.has_value(), errorOf(files));
  made->files = std::move(*files);
  // The species and moves tables come from the packs.
  BattleAdapterDesc desc{};
  desc.files = &*made->files;
  auto battle = std::make_unique<BattleAdapter>(std::move(desc));
  made->battle = battle.get();
  std::vector<std::unique_ptr<SessionSystem>> systems;
  systems.push_back(std::move(battle));
  auto session = GameSession::create({.files = &*made->files,
                                      .rulesetId = "original",
                                      .rules = std::nullopt,
                                      .species = nullptr,
                                      .seed = 7,
                                      .storage = nullptr,
                                      .gameVersion = "test",
                                      .wallClock = [] { return dwe::session::testing::SaveDate; },
                                      .systems = std::move(systems)});
  REQUIRE_MESSAGE(session.has_value(), errorOf(session));
  made->session = std::move(*session);
  return made;
}

// The stats the new-game script (dg 193, set_stat 0..7) leaves the partner with.
struct StartingStats {
  std::int32_t offense = 0;
  std::int32_t defense = 0;
  std::int32_t speed = 0;
  std::int32_t brain = 0;
  std::int32_t hp = 0;
  std::int32_t mp = 0;
};

void checkStartingPartner(const PartnerState& partner, const StartingStats& expected) {
  CHECK(partner.stats.base[Stat::Offense] == expected.offense);
  CHECK(partner.stats.base[Stat::Defense] == expected.defense);
  CHECK(partner.stats.base[Stat::Speed] == expected.speed);
  CHECK(partner.stats.base[Stat::Brain] == expected.brain);
  CHECK(partner.stats.base[Stat::Hp] == expected.hp);
  CHECK(partner.stats.base[Stat::Mp] == expected.mp);
  CHECK(partner.stats.currentHp == expected.hp);
  CHECK(partner.stats.currentMp == expected.mp);
  // partner_impl.c: initializePartner, measured in the original with the second starting species:
  // discipline and happiness 50, 360 hours of life, 3 lives.
  CHECK(partner.mood.discipline == 50);
  CHECK(partner.mood.happiness == 50);
  CHECK(partner.life.remainingLifetime == 360);
  CHECK(partner.life.lives == 3);
  CHECK(partner.life.age == 0);
}

} // namespace

TEST_CASE(
    "the user's disc, extracted: the first partner starts with its first technique and its finisher") {
  const auto extracted = extractedSession();
  if (!extracted) {
    MESSAGE("DWE_CONTENT is not set: skipping the partner's moves on extracted content");
    return;
  }
  const auto started = extracted->session->startNewGame();
  REQUIRE_MESSAGE(started.has_value(), errorOf(started));
  const auto fighter = extracted->battle->partnerCombatant(*extracted->session);
  REQUIRE_MESSAGE(fighter.has_value(), errorOf(fighter));
  // partner_impl.c: initializePartner(3): moves[0] = 0x2E with learnMove(2); the finisher is the
  // list's last.
  CHECK(extracted->battle->moves().learned == std::vector<std::string>{"spit_fire"});
  CHECK(fighter->moves.at(0) == "spit_fire");
  CHECK_FALSE(fighter->moves.at(1).has_value());
  CHECK_FALSE(fighter->moves.at(2).has_value());
  CHECK(fighter->moves.at(dwe::game::battle::FinisherSlot) == "pepper_breath");
  checkStartingPartner(
      extracted->session->partner().state(),
      {.offense = 80, .defense = 60, .speed = 70, .brain = 70, .hp = 800, .mp = 600});
}

TEST_CASE(
    "the user's disc, extracted: the second starting partner starts with its position-2 technique") {
  const auto extracted = extractedSession();
  if (!extracted) {
    MESSAGE("DWE_CONTENT is not set: skipping the second starting partner on extracted content");
    return;
  }
  // The opening's second answer (main.c: main, readPStat(0xFE) != 0).
  dwe::session::NewGameChoice choice{};
  choice.partner = "gabu";
  const auto started = extracted->session->startNewGame(choice);
  REQUIRE_MESSAGE(started.has_value(), errorOf(started));
  const auto fighter = extracted->battle->partnerCombatant(*extracted->session);
  REQUIRE_MESSAGE(fighter.has_value(), errorOf(fighter));
  // partner_impl.c: initializePartner(0x11): moves[0] = 0x30 with learnMove(0x2B). Measured in the
  // original: moves = [0x30, 0xFF, 0xFF, 0x35], learnedMoves = [0, 0x800] (move 43 only). Position
  // 0 would be a 155-power move against this one's 52.
  CHECK(extracted->battle->moves().learned == std::vector<std::string>{"sonic_jab"});
  CHECK(fighter->moves.at(0) == "sonic_jab");
  CHECK_FALSE(fighter->moves.at(1).has_value());
  CHECK_FALSE(fighter->moves.at(2).has_value());
  CHECK(fighter->moves.at(dwe::game::battle::FinisherSlot) == "blue_blaster");
  // dg 193: the other run of the IF (var[103] != 3).
  checkStartingPartner(
      extracted->session->partner().state(),
      {.offense = 70, .defense = 80, .speed = 60, .brain = 70, .hp = 700, .mp = 700});
  // RAISE_DATA[0x11], measured: energy 25, weight 15, poop level 24.
  const PartnerState& partner = extracted->session->partner().state();
  CHECK(partner.hunger.energy == 25);
  CHECK(partner.weight == 15);
  CHECK(partner.toilet.poopLevel == 24);
  CHECK(partner.sleep.sleepyHour == 22);
  CHECK(partner.sleep.wakeHour == 7);
}

TEST_CASE("confirming a technique clears its slot when equipped, else takes the first free one") {
  // game_menu.c: getEquippedSlot and equipMove. Measured in the original (2026-11-04,
  // 03_partner_status): the equipped technique cleared, then back in slot 0; an unknown one changed
  // nothing.
  using dwe::session::MoveSlot;
  using dwe::session::PartnerMoves;
  using dwe::session::TechniqueToggle;
  using dwe::session::toggleTechnique;
  const auto& catalog = battle_test::testCatalog();
  const DigimonRow species =
      madeUpRow("made_up", 3, {"punch", "shot", "poison_bite", "dizzy_ray", "stun_slap"});
  PartnerMoves moves;
  moves.learned = {"dizzy_ray", "poison_bite", "punch", "shot"};
  const auto equipped = [&](std::size_t place) { return moves.equipped.at(place); };

  CHECK(toggleTechnique(moves, species, catalog, 0) == TechniqueToggle::Equipped);
  CHECK(equipped(0) == MoveSlot{0});
  CHECK(toggleTechnique(moves, species, catalog, 0) == TechniqueToggle::Removed);
  CHECK_FALSE(equipped(0).has_value());
  // Not learned, or no move in the species' slot: nothing changes.
  CHECK(toggleTechnique(moves, species, catalog, 4) == TechniqueToggle::Unavailable);
  CHECK(toggleTechnique(moves, species, catalog, 7) == TechniqueToggle::Unavailable);
  CHECK(moves.equipped == PartnerMoves{}.equipped);

  for (const MoveSlot slot : {1, 2, 3}) {
    CHECK(toggleTechnique(moves, species, catalog, slot) == TechniqueToggle::Equipped);
  }
  CHECK(toggleTechnique(moves, species, catalog, 0) == TechniqueToggle::Full);
  // A cleared slot in the middle is the first free one.
  CHECK(toggleTechnique(moves, species, catalog, 2) == TechniqueToggle::Removed);
  CHECK(toggleTechnique(moves, species, catalog, 0) == TechniqueToggle::Equipped);
  CHECK(equipped(1) == MoveSlot{0});
}

TEST_CASE(
    "std_main.c STD_deinitializeCombat: a won match plays song 3, a lost one 4, a draw none") {
  const dwe::session::MatchMusic music{.font = 30, .fight = 2, .won = 3, .lost = 4};
  CHECK(music.endTrack(dwe::game::battle::Outcome::Victory, false) == 3);
  CHECK(music.endTrack(dwe::game::battle::Outcome::Defeat, false) == 4);
  CHECK_FALSE(music.endTrack(dwe::game::battle::Outcome::Defeat, true).has_value());
}
