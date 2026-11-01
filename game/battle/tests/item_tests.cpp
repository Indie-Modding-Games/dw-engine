#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <vector>

#include "fixture.hpp"

using namespace battle_test;

namespace {

ThrownItem healing(std::int32_t hp, std::int32_t mp = 0) {
  ThrownItem item;
  item.hp = hp;
  item.mp = mp;
  return item;
}

ThrownItem disk(std::int32_t offense, bool ignoresLimit = false) {
  ThrownItem item;
  item.boost = StatBoost{.offense = offense,
                         .defense = 0,
                         .speed = 0,
                         .limitPercent = 130,
                         .limitCap = 999,
                         .ignoresLimit = ignoresLimit};
  return item;
}

// main.c: tickThrownItem: the item leaves the hand at the release frame and flies the flight
// frames.
std::int32_t throwFrames(const BattleRules& rules) {
  return rules.itemThrow.releaseFrame + rules.itemThrow.flightFrames;
}

Battle fallenPartner(std::uint64_t seed) {
  Battle battle = startBattle(duel(evenStats(30, 20), evenStats(500, 400)), seed);
  while ((!battle.state().partner().fainted || battle.state().partner().pose != Pose::Fainted) &&
         battle.state().frame < 20000) {
    battle.step();
  }
  REQUIRE(battle.state().partner().fainted);
  return battle;
}

// The amounts of the events of `kind`, in order.
std::vector<std::int32_t> amountsOf(const BattleEvents& events, EventKind kind) {
  std::vector<std::int32_t> amounts;
  for (const BattleEvent& event : events) {
    if (event.kind == kind) {
      amounts.push_back(event.amount);
    }
  }
  return amounts;
}

// Steps until the thrown item lands and returns that frame's events.
BattleEvents stepUntilLanded(Battle& battle) {
  while (battle.itemInFlight() && !battle.finished()) {
    const BattleEvents& events = battle.step();
    if (hasEvent(events, EventKind::ItemLanded)) {
      return events;
    }
  }
  return {};
}

} // namespace

TEST_CASE(
    "the tamer's bag opens only with the tamer, not while fleeing nor with an item on its way") {
  Battle battle = startBattle(duel(evenStats(300, 100), evenStats(300, 100)), 2);
  CHECK(battle.bagAvailable());
  REQUIRE(battle.openBag());
  CHECK(battle.bagOpen());
  // battle_tick.c: handleCommands returns while the bag is open.
  CHECK_FALSE(battle.command(Command::Flee));
  battle.closeBag();
  REQUIRE(battle.throwItem(healing(1)));
  CHECK_FALSE(battle.bagAvailable());
  CHECK_FALSE(battle.openBag());
  CHECK_FALSE(battle.throwItem(healing(1)));

  Battle fleeing = startBattle(duel(evenStats(300, 100), evenStats(300, 100)), 2);
  REQUIRE(fleeing.command(Command::Flee));
  CHECK_FALSE(fleeing.bagAvailable());

  BattleSetup tamerless = duel(evenStats(300, 100), evenStats(300, 100));
  tamerless.tamerless = true;
  CHECK_FALSE(startBattle(tamerless, 2).bagAvailable());
}

TEST_CASE(
    "a thrown item closes the bag and lands on the partner after the throw's frames, the battle going on") {
  Battle battle = startBattle(duel(evenStats(1000, 100), evenStats(1000, 100)), 4);
  battle.step();
  const std::int64_t thrownAt = battle.state().frame;
  REQUIRE(battle.openBag());
  REQUIRE(battle.throwItem(healing(400)));
  CHECK_FALSE(battle.bagOpen());
  CHECK(hasEvent(battle.step(), EventKind::ItemThrown));
  std::int32_t frames = 1;
  while (battle.itemInFlight()) {
    battle.step();
    ++frames;
  }
  CHECK(frames == throwFrames(originalRules()));
  CHECK(battle.state().frame == thrownAt + frames);
}

TEST_CASE(
    "a thrown item heals up to the maximums, cures, protects and raises stats within the disk's limit") {
  Battle battle = startBattle(duel(evenStats(1000, 100), evenStats(1000, 100)), 4);
  Fighter partner = battle.state().partner();
  ThrownItem everything = disk(50);
  everything.hp = 50000;
  everything.mp = 50000;
  everything.curesStatus = true;
  everything.protects = true;
  REQUIRE(battle.throwItem(everything));
  const BattleEvents landed = stepUntilLanded(battle);
  REQUIRE(hasEvent(landed, EventKind::ItemLanded));
  const Fighter& after = battle.state().partner();
  // Healed to the maximums when it landed; the frame may have drained some since (hp + pending
  // stays).
  CHECK(after.hp + after.pendingDamage >= after.stats.maxHp);
  CHECK(after.mp + after.pendingMp >= after.stats.maxMp);
  CHECK(after.status.protectedFromStatus);
  // main.c: handleBuffDisks: 100 at the start allows 130; +50 stops at the limit.
  CHECK(after.stats.offense == std::min(partner.stats.offense + 50, 130));
  REQUIRE(battle.throwItem(disk(20)));
  stepUntilLanded(battle);
  CHECK(battle.state().partner().stats.offense == 130);
  // The omni disk ignores the limit (omni_disk_ignores_limit) and only stops at the cap.
  REQUIRE(battle.throwItem(disk(20, true)));
  stepUntilLanded(battle);
  CHECK(battle.state().partner().stats.offense == 150);
}

TEST_CASE(
    "the defeat countdown waits while a thrown item flies, and a revive gets the partner up") {
  Battle battle = fallenPartner(9);
  // A potion does nothing to a fallen partner but still stops the countdown while it flies.
  battle.step();
  const std::int32_t before = battle.state().deathFrames;
  ThrownItem potion = healing(500);
  potion.consciousOnly = true;
  REQUIRE(battle.throwItem(potion));
  stepUntilLanded(battle);
  CHECK(battle.state().deathFrames == before);
  CHECK(battle.state().partner().fainted);

  ThrownItem restore;
  restore.reviveHpPercent = 50;
  REQUIRE(battle.throwItem(restore));
  const BattleEvents landed = stepUntilLanded(battle);
  CHECK(hasEvent(landed, EventKind::Revived));
  const Fighter& partner = battle.state().partner();
  CHECK_FALSE(partner.fainted);
  CHECK(partner.hp == partner.stats.maxHp / 2);
  CHECK(battle.state().deathFrames == 0);
}

TEST_CASE("a thrown item shows the amounts it heals as the item reads them, HP before MP") {
  Battle battle = startBattle(duel(evenStats(1000, 100), evenStats(1000, 100)), 4);
  // item.c: handleDoubleFloppy: 1500 HP and 1500 MP shown however full the partner is.
  REQUIRE(battle.throwItem(healing(1500, 1500)));
  const BattleEvents landed = stepUntilLanded(battle);
  CHECK(amountsOf(landed, EventKind::HpRestored) == std::vector<std::int32_t>{1500});
  CHECK(amountsOf(landed, EventKind::MpRestored) == std::vector<std::int32_t>{1500});
  const auto hp = std::ranges::find(landed, EventKind::HpRestored, &BattleEvent::kind);
  const auto mp = std::ranges::find(landed, EventKind::MpRestored, &BattleEvent::kind);
  CHECK(hp < mp);
  // A disk heals nothing and shows nothing.
  REQUIRE(battle.throwItem(disk(20)));
  const BattleEvents boosted = stepUntilLanded(battle);
  CHECK_FALSE(hasEvent(boosted, EventKind::HpRestored));
  CHECK_FALSE(hasEvent(boosted, EventKind::MpRestored));
}

TEST_CASE("a revive shows its share of the maximum HP, and the whole revive 9999") {
  Battle battle = fallenPartner(9);
  battle.step();
  ThrownItem potion = healing(500);
  potion.consciousOnly = true;
  REQUIRE(battle.throwItem(potion));
  CHECK_FALSE(hasEvent(stepUntilLanded(battle), EventKind::HpRestored));

  ThrownItem restore;
  restore.reviveHpPercent = 50;
  REQUIRE(battle.throwItem(restore));
  const std::int32_t maxHp = battle.state().partner().stats.maxHp;
  CHECK(amountsOf(stepUntilLanded(battle), EventKind::HpRestored) ==
        std::vector<std::int32_t>{maxHp / 2});

  // item.c: handleRestore, case 0xc: adds 0x270f within the maximum and shows it.
  ThrownItem whole;
  whole.reviveHpPercent = 100;
  REQUIRE(battle.throwItem(whole));
  CHECK(amountsOf(stepUntilLanded(battle), EventKind::HpRestored) ==
        std::vector<std::int32_t>{9999});
  CHECK(battle.state().partner().hp + battle.state().partner().pendingDamage == maxHp);
}

TEST_CASE("the throw's frames come from the ruleset") {
  const BattleRules rules = rulesOf(originalWith("flight_frames: 24", "flight_frames: 4"));
  Battle battle = startBattle(duel(evenStats(1000, 100), evenStats(1000, 100)), 4, rules);
  REQUIRE(battle.throwItem(healing(1)));
  std::int32_t frames = 0;
  while (battle.itemInFlight()) {
    battle.step();
    ++frames;
  }
  CHECK(frames == rules.itemThrow.releaseFrame + 4);
}

TEST_CASE("an enemy's HP bar shows after damage and hides after the HUD's frames") {
  Battle battle = startBattle(duel(evenStats(1000, 300), evenStats(1000, 100)), 3);
  CHECK(battle.hpBarShown(PartnerId));
  CHECK_FALSE(battle.hpBarShown(1));
  bool shown = false;
  bool hiddenAgain = false;
  while (!battle.finished() && battle.state().frame < 20000 && !hiddenAgain) {
    const BattleEvents& events = battle.step();
    const bool hit = std::ranges::any_of(events, [](const BattleEvent& event) {
      return event.kind == EventKind::AttackHit && event.fighter == 1;
    });
    if (hit) {
      CHECK(battle.hpBarShown(1));
      shown = true;
    } else if (shown && !battle.hpBarShown(1)) {
      hiddenAgain = true;
    }
  }
  CHECK(shown);
  CHECK(hiddenAgain);
}

TEST_CASE(
    "main.c tickThrownItem: an item that lands bursts over the partner, a stat disk spins its "
    "disk and a cure with rings shows them for the status it cures") {
  Battle battle = startBattle(duel(evenStats(1000, 100), evenStats(1000, 100)), 4);
  REQUIRE(battle.throwItem(disk(20)));
  stepUntilLanded(battle);
  const StatusVisuals& visuals = battle.statusVisuals();
  CHECK(std::ranges::count_if(visuals.itemBursts(),
                              [](const auto& slot) { return slot.has_value(); }) == 1);
  REQUIRE(visuals.buffDisk().has_value());
  CHECK(visuals.buffDisk()->fighter == PartnerId);
  CHECK(visuals.buffTrails().has_value());
  // No status: the cure shows no rings.
  ThrownItem cure;
  cure.curesStatus = true;
  cure.cureRings = true;
  REQUIRE(battle.throwItem(cure));
  stepUntilLanded(battle);
  CHECK_FALSE(battle.statusVisuals().buffRings().has_value());
}
