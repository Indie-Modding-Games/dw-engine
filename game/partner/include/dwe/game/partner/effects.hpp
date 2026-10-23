#pragma once

#include <dwe/game/partner/care_rules.hpp>
#include <dwe/game/partner/state.hpp>

#include <cstdint>
#include <string>

namespace dwe::game::partner {

// How an offered item may be refused (partner_impl.c: partnerWillRefuseItem). The session decides
// it from the item: some are never taken, some only past a discipline roll, the rest only when they
// are the unwanted favourite food.
enum class RefusalCheck : std::uint8_t { Always, Discipline, FavoriteOnly };

// An item the tamer offers the partner.
struct ItemOffer {
  // The item id (items.yaml), compared with the species' favourite food.
  std::string item;
  RefusalCheck check = RefusalCheck::FavoriteOnly;
};

// A food's boost to the training of some stats.
struct TrainingBoostEffect {
  StatSet stats;
  // The multiplier, in tenths.
  std::int32_t value = 0;
  std::int32_t hours = 0;
};

// What a food does (item.c: handleFood). The inventory owns the item table and the session maps an
// item to its effect; the partner applies it.
struct FoodEffect {
  std::string item;
  // A food that answers hunger (item ids 0x26 to 0x46, 0x79, 0x7a: partnerHandleFoodFeed); the
  // other items that go through handleFood change the stats and needs, but not the hunger.
  bool meal = true;
  std::int32_t energy = 0;
  std::int32_t tirednessRelief = 0;
  std::int32_t happiness = 0;
  std::int32_t discipline = 0;
  std::int32_t weight = 0;
  std::int32_t lifetimeHours = 0;
  // Added to the maximum stats, for good.
  StatBlock permanentGain;
  std::int32_t hpHeal = 0;
  std::int32_t mpHeal = 0;
  TrainingBoostEffect boost;
  std::int32_t sicknessPercent = 0;
};

// What a medicine or a bandage does (item.c: handleStatusItems, handleMedicineHealing): the chance
// to heal an injury and a sickness.
struct CureEffect {
  std::int32_t injuryPercent = 0;
  std::int32_t sicknessPercent = 0;
};

// How the partner feels about the area it is in (map.c: MAP_TILE_DATA.partnerAreaResponse): the map
// table gives the region, the species its favourite one.
enum class AreaResponse : std::uint8_t { Neutral, Liked, Disliked };

// What the care rules need from the rest of the game each tick.
struct CareContext {
  // The tamer is free (tamerGetState() == 0): the original holds back death, poop and the
  // sickness that comes from an injury until then.
  bool tamerIdle = true;
  // No script runs (IS_SCRIPT_PAUSED == 1): death and the life an untreated sickness takes wait
  // for it too.
  bool scriptIdle = true;
  AreaResponse area = AreaResponse::Neutral;
  // The partner is in its normal state (PARTNER_STATE 1, partner.c: partnerTickNormal), not in a
  // care scene: only then do the care rules and the toilet run (tickConditions,
  // tickPoopingMechanic). The game clock goes on either way.
  bool partnerNormal = true;
  // A field plays the partner's scenes: a pile on the ground waits for its scene (partner.c:
  // tickWildPoop), which leaves it with its care effects. Without one the pile and its effects
  // come at once when the urge runs out.
  bool fieldScenes = false;
};

// What sleeping needs from the rest of the game.
struct SleepContext {
  bool hasPillow = false;
  AreaResponse area = AreaResponse::Neutral;
};

enum class BattleResult : std::uint8_t { Won, Fled, Lost };

// A battle as the battle system settled it (tamer.c: startBattle, advanceBattleTime): the battle
// section owns its numbers, and the session gives them to the partner with this.
struct BattleConsequences {
  BattleResult result = BattleResult::Won;
  // It came out injured (battle_ui.c: handleBattleInjury).
  bool injured = false;
  // What the outcome does to the mood, the battles counted for the evolutions and the food level.
  MoodChange mood;
  std::int32_t battles = 0;
  std::int32_t foodChange = 0;
  std::int32_t livesLost = 0;
  // The minutes the clock moves on, whether that ends an hour and the lifetime hours that costs.
  std::int32_t elapsedMinutes = 0;
  bool hourCrossed = false;
  std::int32_t lifetimeHoursLost = 0;
};

// A session at a training place.
struct TrainingRequest {
  // A place and one of its courses (training.yaml: places).
  std::string place;
  std::string course;
  // The bonus roulette's result in tenths; the training section's slot_default_tenths without it.
  std::int32_t slotTenths = 0;
  // The story flag of the course's stat is set (training.yaml: upgrade_flags; isTriggerSet).
  bool gymUpgraded = false;
  bool hasTrainingManual = false;
};

} // namespace dwe::game::partner
