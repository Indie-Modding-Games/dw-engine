#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// The tables of the fishing overlay, `data/fishing` (content/schemas/fishing.schema.json): fish.c's
// FISH_BAIT_CHANCES, FISH_BITE_CHANCE, FISH_KIND_INFO, the shorelines of the water tests and
// FISHING_MAP_DATA. References are an id, a raw number as text, or empty for none. Points are the
// original's screen pixels from the background's optical centre, y down.

// What may get stuck on the hook (FISH_BAIT_CHANCES): the first entry whose threshold is not
// below the roll.
struct FishingJunk {
  std::string item;
  std::int32_t threshold = 0;
};

// A fish kind (FISH_KIND_INFO); the last is the sea serpent.
struct FishingKindEntry {
  // The item a caught fish of this kind gives, as a raw number: the extractor's unknown_0, which
  // is the kind plus 0x3E that FISH_tickRod gives.
  std::int32_t item = 0;
  std::int32_t sizeMin = 0;
  std::int32_t sizeMax = 0;
  std::int32_t moveDelayMin = 0;
  std::int32_t moveDelayMax = 0;
  // The bait that draws it to the bobber.
  std::string bait;
};

// A curve the water tests interpolate: by x (FISH_isFishableWater1) or by y
// (FISH_isFishableWater2).
struct FishingShoreline {
  char axis = 'x';
  std::vector<glm::ivec2> points;
};

// A fish kind's spawn area (FishingSpot): its weight in the population (the extractor's
// unknown_0[0]) and its corners.
struct FishingSpotEntry {
  std::int32_t weight = 0;
  std::vector<glm::ivec2> points;
};

// A map where the player fishes (FISHING_MAP_DATA).
struct FishingMapEntry {
  std::string map;
  // The scale of the ripples of a landed fish and of the bait (the extractor's unknown_8).
  std::int32_t splash = 0;
  // HHMM, 9999 for none: the hours the sea serpent may come.
  std::vector<std::int32_t> seaSerpentTimes;
  std::vector<FishingSpotEntry> spots;
  // Which shoreline its water test walks.
  std::optional<std::size_t> shoreline;
  std::int32_t castRange = 0;
  std::vector<glm::ivec2> seaSerpentPath;
  // FISHING_MAP_DATA's scale in 4096ths: world units per texel of the water's sprites.
  std::int32_t scale = 0;
  // The map's fishing image (its sprite_file) as an image of the base pack:
  // "\\ETCDAT\\FISHING2.TIM" is "texture/etcdat/fishing2". Empty when the data has none.
  std::string image;
  // The fish's scale per size unit (4096 for one texel a unit): small and large fish.
  std::int32_t fishScaleSmall = 0;
  std::int32_t fishScaleLarge = 0;
};

struct FishingTable {
  std::vector<FishingJunk> junk;
  // FISH_BITE_CHANCE: one row per bait from firstBait's number, one percent per kind.
  std::int32_t firstBait = 0;
  std::vector<std::vector<std::int32_t>> biteChances;
  std::vector<FishingKindEntry> kinds;
  std::vector<FishingShoreline> shorelines;
  std::vector<FishingMapEntry> maps;

  // The fishing of a map, by its id, or nullptr.
  [[nodiscard]] const FishingMapEntry* findMap(std::string_view id) const;
};

// Parses the text of `data/fishing`. Unknown and missing keys are errors.
core::Result<FishingTable> parseFishingTable(std::string_view yaml);

// Reads and parses `data/fishing`; nothing when no pack has it (the tests' stand-in base pack).
core::Result<std::optional<FishingTable>> loadFishingTable(const VirtualFileSystem& vfs);

} // namespace dwe::assets
