#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/exe/executable_layout.hpp>
#include <dwe/formats/exe/memory_image.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The gameplay tables of the overlays: fishing (FISH_REL.BIN), the tournament and versus battles (STD_REL.BIN and
// VS_REL.BIN) and training (TRN_REL.BIN, TRN2_REL.BIN). Field names follow the decomp; fields it calls unkN keep the
// name and are converted as they are. A few small tables of those sources live in the main executable instead
// (variables of 8 bytes or less go to its small-data section), so the battle tables also read the executable.
namespace dwe::formats::exe {

// A DVECTOR: a 2D point of the world's x and z.
struct Point16 {
    std::int16_t x = 0;
    std::int16_t y = 0;

    bool operator==(const Point16&) const = default;
};

// FISH_BAIT_CHANCES (fish.h: FishBaitChance).
struct FishBaitChance {
    std::uint8_t item = 0;
    std::uint8_t threshold = 0;

    bool operator==(const FishBaitChance&) const = default;
};

// FISH_KIND_INFO (fish.h: FishKindInfo); kind 6 is the sea serpent of FISH_spawnSeadramon.
struct FishKind {
    std::int32_t unknown0 = 0;
    std::int32_t sizeMin = 0; // lo
    std::int32_t sizeMax = 0; // hi
    std::int32_t moveDelayMin = 0;
    std::int32_t moveDelayMax = 0;
    std::int16_t bait = 0; // an item, compared with the item on the rod; -1 = none
    std::int16_t unknown16 = 0;

    bool operator==(const FishKind&) const = default;
};

// A spawn area of one fish kind (fish.h: FishingSpot).
struct FishingSpot {
    std::array<std::uint8_t, 4> unknown0{}; // unk0 to unk3
    std::int32_t unknown4 = 0;
    std::vector<Point16> points;            // pointCount points
    std::array<std::uint8_t, 3> unknownD{}; // unkD to unkF

    bool operator==(const FishingSpot&) const = default;
};

// FISHING_MAP_DATA (fish.h: FishingMapData). The two function pointers are code; the water test is resolved to the
// shoreline it walks.
struct FishingMap {
    std::int16_t mapId = 0;
    std::int16_t unknown2 = 0;
    std::int32_t scale = 0;
    std::int32_t unknown8 = 0;
    std::int32_t fishScaleSmall = 0;
    std::int32_t fishScaleLarge = 0;
    std::array<std::int16_t, 2> seadramonTimes{}; // HHMM; 9999 = never
    std::vector<FishingSpot> spots;
    std::int32_t unknown1C = 0;
    std::optional<std::size_t> shoreline; // which FishingTables::shorelines its water test walks
    std::int16_t castRange = 0;
    std::int16_t unknown2A = 0;
    std::string spriteFile;
    std::vector<Point16> seadramonWaypoints;

    bool operator==(const FishingMap&) const = default;
};

// A shoreline curve of the water test: FISH_isFishableWater1 interpolates its points by x, FISH_isFishableWater2 by
// y (FISH_lerpTableByX, FISH_lerpTableByY).
struct FishingShoreline {
    char axis = 'x';
    std::vector<Point16> points;

    bool operator==(const FishingShoreline&) const = default;
};

struct FishingTables {
    std::vector<FishBaitChance> baitChances;
    // FISH_BITE_CHANCE: one row per depth from firstBiteDepth, one column per fish kind (FISH_findFishToBite).
    std::size_t firstBiteDepth = 0;
    std::vector<std::vector<std::int8_t>> biteChances;
    std::vector<std::int8_t> unusedBiteBytes; // after the last row; no code reads them
    std::vector<FishKind> kinds;
    std::vector<FishingMap> maps;
    std::vector<FishingShoreline> shorelines;

    bool operator==(const FishingTables&) const = default;
};

// A shoreline of the fishing layout: where its points are and the code address of the water test that walks it.
struct ShorelineLocation {
    TableLocation points;
    char axis = 'x';
    std::uint32_t waterTest = 0;
};

struct FishingLayout {
    TableLocation baitChances;
    TableLocation biteChances; // bytes
    std::size_t biteKinds = 0;
    std::size_t biteRows = 0;
    std::size_t firstBiteDepth = 0;
    TableLocation kinds;
    TableLocation maps;
    std::size_t spotsPerMap = 0; // the code walks this many; FishingMapData has no count
    std::array<ShorelineLocation, 2> shorelines{};
};

// CameraPreset (battle.h): six values passed in order to STD_setCameraParams / VS_setCameraParams.
struct CameraPreset {
    std::array<std::int16_t, 6> values{};

    bool operator==(const CameraPreset&) const = default;
};

// An arena: the files STD_loadArenaAssets / VS_loadArenaAssets load and how many objects and TIMs they hold.
struct BattleArena {
    std::size_t index = 0; // the arena id
    std::string model;
    std::string texture;
    std::string collision;
    std::uint8_t modelCount = 0;
    std::uint8_t textureCount = 0;

    bool operator==(const BattleArena&) const = default;
};

// The tables of a battle overlay. The tournament (STD) has all of them; the versus mode (VS) only cameras and
// arenas.
struct BattleStageTables {
    std::vector<std::int16_t> opponentBaseStats;         // by tournament (std_setup.c: STD_initializeOpponent)
    std::vector<std::string> grades;                     // TIM per tournament (STD_initializeTournament)
    std::vector<std::array<std::uint8_t, 3>> winChances; // [levelA - 3][levelB - 3] (STD_setupParticipants)
    std::vector<CameraPreset> cameraPresets;
    std::vector<BattleArena> arenas; // the arenas with files; entries whose paths are all null are left out

    bool operator==(const BattleStageTables&) const = default;
};

struct BattleStageLayout {
    TableLocation opponentBaseStats; // not located = the overlay has none
    TableLocation grades;
    TableLocation winChances; // rows
    TableLocation cameraPresets;
    TableLocation arenaModels; // char*[]
    TableLocation arenaTextures;
    TableLocation arenaCollisions;
    TableLocation arenaModelCounts; // u8[], in the main executable
    TableLocation arenaTextureCounts;
};

// TRN_TRAINING_ANIM_IDS: per partner type, the animation and the frame the hit lands on.
struct TrainingAnimation {
    std::int8_t animation = 0;
    std::int8_t hitFrame = 0;

    bool operator==(const TrainingAnimation&) const = default;
};

// training.h: TrainingSpot; stat is the kind of training.
struct TrainingSpot {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t z = 0;
    std::int16_t stat = 0;

    bool operator==(const TrainingSpot&) const = default;
};

// training.h: MapTrainingSpots.
struct TrainingMapSpots {
    std::int32_t mapId = 0;
    std::vector<TrainingSpot> spots;

    bool operator==(const TrainingMapSpots&) const = default;
};

struct TrainingTables {
    std::vector<TrainingAnimation> animations;
    // trn_reward.c: TRN_tryLearnMove: the moves of each special type (8 each) and their weights [k][special slot].
    // Empty in an overlay without them.
    std::vector<std::vector<std::uint8_t>> learnableMoves;
    std::vector<std::vector<std::int8_t>> learnWeights;
    std::vector<std::vector<std::int8_t>> slotReels; // trn_slots.c: TRN_tickSlotMachine
    std::vector<std::int8_t> assistRolls;            // TRN_createSlotMachineBox: one of them at random
    std::vector<TrainingMapSpots> spots;             // trn_hud.c: TRN_getTrainingSpotScreenPos

    bool operator==(const TrainingTables&) const = default;
};

struct TrainingLayout {
    TableLocation animations;
    TableLocation learnableMoves; // rows of learnableMovesWidth; not located = none
    std::size_t learnableMovesWidth = 0;
    TableLocation learnWeights; // rows of learnWeightsWidth
    std::size_t learnWeightsWidth = 0;
    TableLocation slotReels; // rows of slotReelWidth
    std::size_t slotReelWidth = 0;
    TableLocation assistRolls;
    TableLocation spots; // at most count records; mapId -1 ends the list, stat -1 ends each map's spots
};

// The overlay tables of one version.
struct OverlayTablesLayout {
    std::string_view version;
    FishingLayout fishing;
    BattleStageLayout stadium;
    BattleStageLayout versus;
    TrainingLayout training;
    TrainingLayout training2;
};

// Returns the overlay tables of a version, if the tools know them.
std::optional<OverlayTablesLayout> findOverlayTablesLayout(std::string_view version);

core::Result<FishingTables> readFishingTables(const MemoryImage& overlay, const FishingLayout& layout);
core::Result<BattleStageTables> readBattleStageTables(const MemoryImage& overlay, const MemoryImage& executable,
                                                      const BattleStageLayout& layout);
core::Result<TrainingTables> readTrainingTables(const MemoryImage& overlay, const TrainingLayout& layout);

} // namespace dwe::formats::exe
