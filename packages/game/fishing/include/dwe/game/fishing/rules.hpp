#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The numbers of the fishing (the FISH overlay: fish.c; main/fish.c). The rules of its logic come
// from the ruleset's `fishing` section; its tables (fish kinds, bite chances, spots, shorelines)
// come from data/fishing, resolved by the caller into FishingTables and FishingPlace. Items are
// item numbers (ItemEntry::index). Spots, shorelines and the sea serpent's path are in the
// original's screen pixels from the background's optical centre, y down (FISH_projectPoint).
namespace dwe::game::fishing {

inline constexpr std::string_view FishingSectionName = "fishing";

// A whole-number range, both ends included (customRandom(lo, hi)).
struct Range {
  std::int32_t min = 0;
  std::int32_t max = 0;
};

struct SeaSerpentRules {
  std::int32_t biteChance = 0;
  std::int32_t biteEvery = 0;
  // How often the population looks for the sea serpent's hour.
  std::int32_t checkEvery = 0;
  // The map-head section of the scene that plays once it is landed (triggerSeadramonCutscene).
  std::int32_t sceneSection = 0;
};

struct BiteRules {
  // A bite is rolled every this many frames, for fish within `reach` of the bobber; a fish that
  // likes the bait swims to the bobber from closer than attractReach.
  std::int32_t every = 0;
  std::int32_t reach = 0;
  std::int32_t attractReach = 0;
};

struct PopulationRules {
  std::int32_t pool = 0;
  std::int32_t spawnBelow = 0;
  Range lifetime;
  // Frames without a new fish after one was scared off.
  std::int32_t scaredPause = 0;
};

struct HookTiming {
  Range nibble;
  std::int32_t pull = 0;
};

struct HookRules {
  std::int32_t largeSize = 0;
  HookTiming small;
  HookTiming large;
  std::int32_t powerBonus = 0;
  std::int32_t powerMaxBonus = 0;
};

struct TensionRules {
  std::int32_t oldRodLimit = 0;
  std::int32_t amazingRodLimit = 0;
  // The sizes the deltas map between.
  Range sizes;
  Range slack;
  Range fishRunning;
  Range held;
  Range heldRunning;
  Range snapSteps;
  std::int32_t snapFramesPerStep = 0;
};

struct JunkRules {
  std::int32_t after = 0;
  std::int32_t every = 0;
  // A roll of 1 to `roll` at or below `chance` hooks something; 0 to `pick` chooses it.
  std::int32_t chance = 0;
  std::int32_t roll = 0;
  std::int32_t pick = 0;
};

struct CastRules {
  std::int32_t powerBonus = 0;
  std::int32_t powerMax = 0;
  std::int32_t minSpeed = 0;
  std::int32_t oldRodSpeed = 0;
  std::int32_t amazingRodSpeed = 0;
};

// FISH_isFishableWater2: nothing from rightLimit on, and everything above openAbove.
struct ShoreByYRules {
  std::int32_t rightLimit = 0;
  std::int32_t openAbove = 0;
};

struct FishingSounds {
  std::int32_t slot = 0;
  std::int32_t cast = 0;
  std::int32_t splash = 0;
  std::int32_t nibble = 0;
  std::int32_t bite = 0;
  std::int32_t struggle = 0;
  std::int32_t run = 0;
  std::int32_t landed = 0;
  std::int32_t item = 0;
  std::int32_t lost = 0;
  std::int32_t notice = 0;
};

struct FishingRules {
  SeaSerpentRules seaSerpent;
  BiteRules bites;
  PopulationRules population;
  HookRules hook;
  TensionRules tension;
  JunkRules junk;
  CastRules cast;
  ShoreByYRules shoreByY;
  FishingSounds sounds;
};

// Reads the `fishing` section; errors name the line.
core::Result<FishingRules> parseFishingRules(const ruleset::Section& section);

// A fish kind (FISH_KIND_INFO).
struct FishKind {
  // The item a caught fish gives.
  std::int32_t item = 0;
  Range size;
  // Frames between two moves while it idles.
  Range moveDelay;
  // The bait that draws it to the bobber.
  std::optional<std::int32_t> attractedBy;
};

// What may get stuck on the hook: the first whose upTo is not below the roll.
struct JunkItem {
  std::int32_t item = 0;
  std::int32_t upTo = 0;
};

// The tables every fishing map shares.
struct FishingTables {
  // The fish kinds, then the sea serpent.
  std::vector<FishKind> kinds;
  FishKind seaSerpent;
  // Percent by bait row (from firstBait's item number) and fish kind.
  std::int32_t firstBait = 0;
  std::vector<std::vector<std::int32_t>> biteChances;
  std::vector<JunkItem> junk;
};

// Where the water is on a map's screen (FISH_isFishableWater1 and 2).
struct Shoreline {
  enum class Axis : std::uint8_t {
    // Water below the line, and past either end of it.
    X,
    // Water left of the line, within ShoreByYRules.
    Y,
  };
  Axis axis = Axis::X;
  std::vector<glm::ivec2> points;
};

struct FishingSpot {
  std::int32_t weight = 0;
  std::vector<glm::ivec2> points;
};

// A map where the player fishes (FISHING_MAP_DATA).
struct FishingPlace {
  std::int32_t castRange = 0;
  std::int32_t splash = 0;
  // HHMM, the hours the sea serpent may come; 9999 for none.
  std::vector<std::int32_t> seaSerpentTimes;
  Shoreline shoreline;
  // One per fish kind, in order.
  std::vector<FishingSpot> spots;
  std::vector<glm::ivec2> seaSerpentPath;
  // How the map draws it (FISHING_MAP_DATA): the sprite image (an asset of the packs, such as
  // "texture/etcdat/fishing2"), the bobber's scale (4096 for one texel a unit) and the fish's per
  // size unit, small and large.
  std::string spriteImage;
  std::int32_t spriteScale = 0;
  std::int32_t fishScaleSmall = 0;
  std::int32_t fishScaleLarge = 0;
};

// Whether a screen point is water (FISH_isFishableWater1 and 2).
[[nodiscard]] bool isWater(const Shoreline& shoreline, const ShoreByYRules& byY, glm::ivec2 point);

// FISH_remapClamped: x clamped to [from.min, from.max] mapped onto [to.min, to.max], rounded.
[[nodiscard]] std::int32_t remapClamped(std::int32_t x, Range from, Range to);

} // namespace dwe::game::fishing
