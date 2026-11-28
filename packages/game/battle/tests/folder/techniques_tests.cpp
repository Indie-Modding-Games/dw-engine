#include <dwe/game/battle/boosted_moves.hpp>
#include <dwe/game/battle/folder/formulas.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "folder_kit.hpp"

using namespace folder_test;
using dwe::game::battle::folder::Technique;

namespace {

constexpr std::uint8_t GuestOwner = 1;

Technique techniqueNamed(const FolderRules& rules, std::string_view id) {
  return folder::techniqueOf(rules, testCatalog(), refOf(testCatalog(), id));
}

TechniqueFunctions bits(std::initializer_list<TechniqueFunction> functions) {
  TechniqueFunctions all = 0;
  for (const TechniqueFunction function : functions) {
    all |= static_cast<TechniqueFunctions>(function);
  }
  return all;
}

Technique costed(ReachClass reach, AreaClass area) {
  return {.functions = 0,
          .area = AreaShape::Single,
          .areaPixels = 0,
          .friendlyFire = false,
          .reach = reach,
          .areaClass = area,
          .mpCost = 0};
}

// The partner beside a co-op ally, both holding their ground, against one enemy within reach and a
// second one far away; the partner knows the area move.
BattleSetup blastSetup(BattleSystemId system) {
  BattleSetup setup = duel(evenStats(3000, 100), evenStats(3000, 100), {"wide_blast", "punch"});
  setup.system = system;
  setup.enemies.front().position = {.x = 0, .z = 400};
  setup.enemies.push_back(fighterWith("far", evenStats(3000, 100), {""}, {.x = 0, .z = 6000}));
  setup.allies.push_back(
      {.partner = fighterWith("buddy", evenStats(3000, 100), {"punch"}, {.x = 300, .z = 0}),
       .condition =
           {.discipline = 50, .tiredness = 0, .sick = false, .injured = false, .knownMoves = {}},
       .tamer = {.x = 300, .z = -800},
       .owner = GuestOwner});
  return setup;
}

bool hitBy(const BattleEvents& events, FighterId fighter, FighterId attacker) {
  return std::ranges::any_of(events, [&](const BattleEvent& event) {
    return event.kind == EventKind::AttackHit && event.fighter == fighter &&
           event.other == attacker;
  });
}

} // namespace

TEST_CASE("a folder technique reads its function, area and reach from the move table") {
  const FolderRules& rules = folderRules();
  const Technique shot = techniqueNamed(rules, "shot");
  CHECK(shot.functions == bits({TechniqueFunction::Offensive}));
  CHECK(shot.area == AreaShape::Single);
  CHECK_FALSE(shot.friendlyFire);
  CHECK(shot.reach == ReachClass::Medium);

  const Technique blast = techniqueNamed(rules, "wide_blast");
  CHECK(blast.area == AreaShape::AroundSelf);
  CHECK(blast.areaPixels == 500);
  CHECK(blast.areaClass == AreaClass::Medium);
  CHECK(blast.reach == ReachClass::Medium);
  CHECK(blast.friendlyFire);

  const Technique slap = techniqueNamed(rules, "stun_slap");
  CHECK(slap.functions == bits({TechniqueFunction::Offensive, TechniqueFunction::Control}));
  CHECK(slap.reach == ReachClass::Short);

  const Technique charge = techniqueNamed(rules, "muscle_charge");
  CHECK(charge.functions == bits({TechniqueFunction::Support}));
  CHECK(charge.reach == ReachClass::Personal);
  CHECK(techniqueNamed(rules, "counter").serves(TechniqueFunction::Defensive));
}

TEST_CASE("a folder technique costs what costo-mp.md says") {
  const FolderRules& rules = folderRules();
  // 40 x 30 % + medium reach 30.
  CHECK(techniqueNamed(rules, "shot").mpCost == 42);
  // 35 x 30 % + medium reach 30 + a medium area 40, rounded up.
  CHECK(techniqueNamed(rules, "wide_blast").mpCost == 81);
  // 20 x 30 % + stun 50 x 100 %.
  CHECK(techniqueNamed(rules, "stun_slap").mpCost == 56);
  // A strong boost of one stat (ATK +30 %) for the whole battle: 60 x 1.5.
  CHECK(techniqueNamed(rules, "muscle_charge").mpCost == 90);
  // ATK +10, DEF +5, VEL +10: a light boost of several stats, 45 x 1.5, rounded up.
  CHECK(techniqueNamed(rules, "war_cry").mpCost == 68);
  CHECK(techniqueNamed(rules, "big_finish").mpCost == 0);

  // costo-mp.md §9, examples 1 to 3.
  BattleMove move;
  move.power = 100;
  CHECK(folder::mpCostOf(rules, move, costed(ReachClass::Medium, AreaClass::Single),
                         EffectDuration::Short) == 60);
  move.power = 200;
  move.status = StatusEffect::Stun;
  move.statusChance = 25;
  CHECK(folder::mpCostOf(rules, move, costed(ReachClass::Long, AreaClass::Single),
                         EffectDuration::Short) == 133);
  move.power = 150;
  move.status = StatusEffect::Poison;
  move.statusChance = 50;
  CHECK(folder::mpCostOf(rules, move, costed(ReachClass::Long, AreaClass::Large),
                         EffectDuration::Medium) == 187);
}

TEST_CASE("a boosted technique keeps its discount on the folder cost") {
  MoveCatalog moves = testCatalog();
  const MoveRef shot = refOf(moves, "shot");
  const MoveRef boosted = moves.addBoosted(
      shot, BoostedMoveRules{.powerPercent = 125, .mpCostPercent = 75, .species = {}});
  // The table's 30 MP become 23: 42 x 23 / 30, rounded up.
  CHECK(folder::techniqueOf(folderRules(), moves, boosted).mpCost == 33);
  CHECK(folder::techniqueOf(folderRules(), moves, boosted).reach == ReachClass::Medium);
}

TEST_CASE("a technique's catalog entry changes what the move table says") {
  const BattleRules rules = rulesOf(originalWith(
      "  catalog: {}", "  catalog:\n    shot: {area: around_target, area_pixels: 250, functions: "
                       "[offensive, control], friendly_fire: false, mp_cost: 12}"));
  REQUIRE(rules.folder.has_value());
  const Technique shot = techniqueNamed(*rules.folder, "shot");
  CHECK(shot.area == AreaShape::AroundTarget);
  CHECK(shot.areaPixels == 250);
  CHECK(shot.areaClass == AreaClass::Small);
  CHECK(shot.functions == bits({TechniqueFunction::Offensive, TechniqueFunction::Control}));
  CHECK_FALSE(shot.friendlyFire);
  CHECK(shot.mpCost == 12);

  const auto wrong = readBattleRules(
      sectionOf(originalWith("  catalog: {}", "  catalog:\n    shot: {area: everywhere}")));
  REQUIRE_FALSE(wrong);
  CHECK(wrong.error().message.find("everywhere") != std::string::npos);
}

TEST_CASE("the folder cover chance gains points the closer the attacker") {
  const FolderRules& rules = folderRules();
  CHECK(folder::coverDistanceBonus(rules, 0) == 10);
  CHECK(folder::coverDistanceBonus(rules, 400) == 5);
  CHECK(folder::coverDistanceBonus(rules, 800) == 0);
  CHECK(folder::coverDistanceBonus(rules, 5000) == 0);
}

TEST_CASE("the folder battle charges and shows the technique's folder cost") {
  BattleSetup setup = folderDuel(evenStats(3000, 100), evenStats(3000, 100));
  setup.partner.mp = 41;
  Battle poor = startBattle(setup, 1);
  CHECK(poor.partnerMoveCost(1) == 42);
  CHECK(poor.orderProblem(1) == OrderProblem::NotEnoughMp);
  REQUIRE(poor.folderTechnique(1).has_value());
  CHECK(poor.folderTechnique(1)->mpCost == 42);
  CHECK_FALSE(poor.folderTechnique(3).has_value());

  setup.partner.mp = 42;
  Battle battle = startBattle(setup, 1);
  CHECK(battle.orderProblem(1) == OrderProblem::None);
  REQUIRE(battle.order(1, 1));
  bool started = false;
  for (int frame = 0; frame < 400 && !started; ++frame) {
    started = hasEvent(battle.step(), EventKind::AttackStarted);
  }
  REQUIRE(started);
  const Fighter& partner = battle.state().fighters[PartnerId];
  CHECK(partner.mp - partner.pendingMp == 0);

  Battle classic =
      startBattle(duel(evenStats(3000, 100), evenStats(3000, 100), {"punch", "shot"}), 1);
  CHECK(classic.partnerMoveCost(1) == 30);
  CHECK_FALSE(classic.folderTechnique(1).has_value());
}

TEST_CASE("a folder area technique hits whoever stands in its area, allies too") {
  Battle battle = startBattle(blastSetup(BattleSystemId::Folder), 4);
  const FighterId ally = battle.state().allies.front().id;
  REQUIRE(battle.setStance(Stance::Hold));
  REQUIRE(battle.setStance(Stance::Hold, ally));
  REQUIRE(battle.order(0, 1));
  BattleEvents events;
  for (int frame = 0; frame < 300 && !hasEvent(events, EventKind::OrderDone); ++frame) {
    const BattleEvents& step = battle.step();
    events.insert(events.end(), step.begin(), step.end());
  }
  CHECK(hitBy(events, 1, PartnerId));
  CHECK(hitBy(events, ally, PartnerId));
  CHECK_FALSE(hitBy(events, 2, PartnerId));
}

TEST_CASE("the classic battle keeps the partners' area moves off each other") {
  Battle battle = startBattle(blastSetup(BattleSystemId::Classic), 4);
  const FighterId ally = battle.state().allies.front().id;
  const BattleEvents events = runFrames(battle, 1500);
  CHECK_FALSE(hitBy(events, ally, PartnerId));
  CHECK_FALSE(hitBy(events, PartnerId, ally));
}

TEST_CASE("the folder checksum covers each order's target") {
  BattleSetup setup = folderDuel(evenStats(3000, 100), evenStats(3000, 100));
  setup.enemies.push_back(fighterWith("foe2", evenStats(3000, 100), {"punch"}, {.x = 600, .z = 0}));
  Battle first = startBattle(setup, 9);
  Battle second = startBattle(setup, 9);
  REQUIRE(first.checksum() == second.checksum());
  REQUIRE(first.order(0, 1));
  REQUIRE(second.order(0, 2));
  CHECK(first.checksum() != second.checksum());
}

TEST_CASE("co-op difficulty scales a folder battle's enemies as it scales a classic one's") {
  for (const BattleSystemId system : {BattleSystemId::Classic, BattleSystemId::Folder}) {
    BattleSetup setup = blastSetup(system);
    setup.coopScaling = {.level = CoopLevel::Standard, .helpers = 0};
    const Battle battle = startBattle(setup, 7);
    CHECK(battle.scaling() ==
          ScalingFactors{.hp = 180, .mp = 150, .offense = 100, .finisherFill = 125});
    CHECK(battle.state().fighters.at(1).stats.maxHp == 5400);
    CHECK(battle.state().fighters.at(1).startStats.maxHp == 3000);
  }
}
