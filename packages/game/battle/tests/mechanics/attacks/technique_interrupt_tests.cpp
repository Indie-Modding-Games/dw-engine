#include <dwe/game/battle/battle_system_id.hpp>
#include <dwe/game/battle/state.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "../../bench.hpp"
#include "core/battle_system.hpp"
#include "mechanics/status/status.hpp"

using namespace battle_test;

namespace {

constexpr std::int32_t Age = 8;

// The partner's shot on its way to the enemy, `Age` attack frames after it started.
AttackInFlight partnerShot(std::optional<std::int32_t> committed) {
  return {.attacker = PartnerId,
          .defender = 1,
          .move = Bench::move("shot"),
          .origin = {},
          .framesLeft = 10,
          .age = Age,
          .clip = std::nullopt,
          .hitPoint = std::nullopt,
          .touch = std::nullopt,
          .facingTarget = {},
          .aim = std::nullopt,
          .laterContacts = {},
          .contacted = false,
          .committed = committed};
}

AttackInFlight enemyPunch() {
  return {.attacker = 1,
          .defender = PartnerId,
          .move = Bench::move("punch"),
          .origin = {},
          .framesLeft = 0,
          .age = std::nullopt,
          .clip = std::nullopt,
          .hitPoint = std::nullopt,
          .touch = std::nullopt,
          .facingTarget = {},
          .aim = std::nullopt,
          .laterContacts = {},
          .contacted = false,
          .committed = std::nullopt};
}

void startShot(Bench& bench) {
  bench.fighter(PartnerId).attack = ActiveAttack{.move = Bench::move("shot"),
                                                 .target = FighterId{1},
                                                 .frame = Age,
                                                 .length = 40,
                                                 .releaseFrame = 0,
                                                 .released = true,
                                                 .followed = true};
  bench.fighter(PartnerId).pose = Pose::Attacking;
}

std::vector<std::optional<std::int32_t>> flyingCommits(const Bench& bench) {
  std::vector<std::optional<std::int32_t>> commits;
  for (const AttackInFlight& flying : bench.state.inFlight) {
    if (flying.attacker == PartnerId) {
      commits.push_back(flying.committed);
    }
  }
  return commits;
}

BattleSetup shotDuel(BattleSystemId system) {
  BattleSetup setup = duel(evenStats(800, 100), evenStats(800, 100), {"punch", "shot"}, {"punch"});
  setup.system = system;
  return setup;
}

// The partner shooting `committed` with the shot of an order it is executing; its broken defense
// never covers, so a punch lands.
void executeShotOrder(Bench& bench, std::optional<std::int32_t> committed) {
  if (bench.state.folder) {
    bench.system = makeBattleSystem(BattleSystemId::Folder);
    FolderFighter& own = bench.state.folder->fighters.at(PartnerId);
    own.active = Order{.slot = 1, .target = FighterId{1}, .id = 1};
    own.phase = OrderPhase::Executing;
    own.brokenFrames = 100;
  }
  startShot(bench);
  bench.state.inFlight = {partnerShot(committed)};
}

} // namespace

// battle_main.c: BTL_tickAttackHits calls BTL_removeMoveEffect on the fighter a hit reaches, which
// stops only the sub-effect BTL_startEFE returned (battle_effect.c: BTL_stopEFESubEffect). Measured
// in the original: Spit Fire's fireball is a sub-effect of its own, spawned 29 of its 53 frames
// in; a hit before it loses the technique, a hit after it lands the 84 damage all the same.
TEST_CASE("a hit on the attacker keeps the part of its technique already committed") {
  Bench bench(shotDuel(BattleSystemId::Classic), 3);
  startShot(bench);
  bench.state.inFlight = {partnerShot(Age - 3), partnerShot(Age), partnerShot(std::nullopt),
                          partnerShot(-1)};
  BattleContext context = bench.context();
  context.system->handleAttack(context, enemyPunch());
  CHECK_FALSE(bench.fighter(PartnerId).attack.has_value());
  CHECK(flyingCommits(bench) == std::vector<std::optional<std::int32_t>>{Age - 3, -1});
}

// battle_main.c: BTL_tickStatusEffects: a poison tick only adds to hpDamageBuffer and shows the
// number; the attack, its effect and its pose go on (checked live: the effect's root sub-effect and
// the attacking flag survive the tick).
TEST_CASE("a poison tick never interrupts a technique, Classic or Folder") {
  for (const BattleSystemId system : {BattleSystemId::Classic, BattleSystemId::Folder}) {
    CAPTURE(static_cast<int>(system));
    Bench bench(shotDuel(system), 3);
    executeShotOrder(bench, std::nullopt);
    Fighter& partner = bench.fighter(PartnerId);
    partner.status.poisoned = true;
    partner.status.poisonFrames = 1;
    BattleContext context = bench.context();
    tickStatusEffects(context);
    REQUIRE(std::ranges::any_of(bench.events, [](const BattleEvent& event) {
      return event.kind == EventKind::PoisonDamage && event.fighter == PartnerId;
    }));
    context.system->tickDecisions(context);
    CHECK(partner.attack.has_value());
    CHECK(partner.pose == Pose::Attacking);
    CHECK(flyingCommits(bench).size() == 1);
    if (system == BattleSystemId::Folder) {
      CHECK(bench.state.folder->fighters.at(PartnerId).phase == OrderPhase::Executing);
    }
  }
}

// folder-battle.md "Interrupted by a hit": the original's commit point holds in Folder too. A hit
// once the technique committed lets it land and the order counts as done; a hit before it sends
// the order back to positioning.
TEST_CASE("a folder order hit after its technique committed is done, before it is retried") {
  for (const auto& [committed, phase] :
       {std::pair{std::optional<std::int32_t>{Age - 3}, OrderPhase::Recovering},
        std::pair{std::optional<std::int32_t>{}, OrderPhase::Positioning}}) {
    Bench bench(shotDuel(BattleSystemId::Folder), 3);
    executeShotOrder(bench, committed);
    BattleContext context = bench.context();
    context.system->handleAttack(context, enemyPunch());
    REQUIRE_FALSE(bench.fighter(PartnerId).attack.has_value());
    context.system->tickDecisions(context);
    CHECK(bench.state.folder->fighters.at(PartnerId).phase == phase);
    CHECK(flyingCommits(bench).size() == (committed ? 1U : 0U));
    CHECK_FALSE(bench.state.folder->fighters.at(PartnerId).committed);
  }
}
