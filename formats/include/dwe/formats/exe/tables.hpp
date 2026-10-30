#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/exe/executable_layout.hpp>
#include <dwe/formats/exe/item_effects.hpp>
#include <dwe/formats/exe/memory_image.hpp>
#include <dwe/formats/exe/music_code.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The gameplay and map tables of the main executable. Structs and field names follow the decomp
// (include/dw/*.h); fields it does not name are unknownXX, with XX the offset in the struct. Text
// fields keep the raw bytes of the executable: plain ASCII in the US release, Shift-JIS (cp932)
// elsewhere.
namespace dwe::formats::exe {

// params.h: DigimonPara. DIGIMON_DATA is indexed by Digimon type.
struct DigimonPara {
  std::string name; // empty in versions without inline names
  std::int32_t boneCount = 0;
  std::int16_t radius = 0;
  std::int16_t height = 0;
  std::uint8_t type = 0;
  std::uint8_t level = 0;
  std::array<std::uint8_t, 3> special{}; // 0xFF = none
  std::uint8_t dropItem = 0;             // 0xFF = none
  std::uint8_t dropChance = 0;
  std::array<std::uint8_t, 16>
      moves{}; // move ids, 0xFF = none; slot i is the move of animation 0x2E + i

  bool operator==(const DigimonPara&) const = default;
};

// model.h: SkeletonBone. -1 = no object / no parent.
struct SkeletonBone {
  std::int8_t objIndex = -1;
  std::int8_t parentIndex = -1;

  bool operator==(const SkeletonBone&) const = default;
};

// move.h: Move.
struct Move {
  std::int32_t distance = 0;
  std::int16_t power = 0;
  std::uint8_t mpCost = 0;
  std::uint8_t iframes = 0;
  std::uint8_t range = 0;
  std::uint8_t special = 0;
  std::uint8_t status = 0;
  std::uint8_t accuracy = 0;
  std::uint8_t statusChance = 0;
  std::uint8_t unknown0D = 0;
  std::uint8_t unknown0E = 0;
  std::uint8_t unknown0F = 0;

  bool operator==(const Move&) const = default;
};

// item.h: Item.
struct Item {
  std::string name; // empty in versions without inline names
  std::int32_t value = 0;
  std::uint16_t meritValue = 0;
  std::int16_t sortingValue = 0;
  std::uint8_t itemColor = 0;
  std::uint8_t droppable = 0;
  std::uint16_t unknown1E = 0;

  bool operator==(const Item&) const = default;
};

// evl.h: EvoRequirements, indexed by the target Digimon type. -1 = no requirement.
struct EvoRequirements {
  std::int16_t digimon = 0;
  std::int16_t hp = 0;
  std::int16_t mp = 0;
  std::int16_t offense = 0;
  std::int16_t defense = 0;
  std::int16_t speed = 0;
  std::int16_t brain = 0;
  std::int16_t care = 0;
  std::int16_t weight = 0;
  std::int16_t discipline = 0;
  std::int16_t happiness = 0;
  std::int16_t battles = 0;
  std::int16_t techs = 0;
  std::int8_t flags = 0; // 0x01 = battles is a maximum, 0x10 = care is a maximum (evolution.c)

  bool operator==(const EvoRequirements&) const = default;
};

// evl.h: EvoStatsGains, indexed by the target Digimon type.
struct EvoStatsGains {
  std::int16_t hp = 0;
  std::int16_t mp = 0;
  std::int16_t offense = 0;
  std::int16_t defense = 0;
  std::int16_t speed = 0;
  std::int16_t brains = 0;
  std::int16_t targetDigimon = 0;

  bool operator==(const EvoStatsGains&) const = default;
};

// evl.h: EvolutionPath. Entry i belongs to Digimon type i + 1 (evolution.c, game_menu.c); -1 =
// none.
struct EvolutionPath {
  std::array<std::int8_t, 5> from{};
  std::array<std::int8_t, 6> to{};

  bool operator==(const EvolutionPath&) const = default;
};

// partner.h: RaiseData, indexed by Digimon type.
struct EvolutionChartEntry {
  std::int16_t x = 0;
  std::int16_t y = 0;
  std::uint8_t u = 0;
  std::uint8_t v = 0;
  std::uint8_t palette = 0;
};

struct RaiseData {
  std::array<std::int8_t, 8> hungerTimes{};
  std::uint8_t energyCap = 0;
  std::uint8_t energyThreshold = 0;
  std::uint8_t energyUsage = 0;
  std::int16_t poopTimer = 0;
  std::int16_t unknown0E = 0;
  std::uint8_t poopSize = 0;
  std::uint8_t favoriteFood = 0;
  std::uint8_t sleepCycle = 0;
  std::uint8_t favoredRegion = 0;
  std::uint8_t trainingType = 0;
  std::uint8_t defaultWeight = 0;
  std::int16_t viewX = 0;
  std::int16_t viewY = 0;
  std::int16_t viewZ = 0;

  bool operator==(const RaiseData&) const = default;
};

// script.h: CardData, indexed by card id.
struct CardData {
  std::uint8_t digimonId = 0;
  std::uint8_t spriteId = 0;
  std::int16_t meritValue = 0;

  bool operator==(const CardData&) const = default;
};

// map.h: MapEntry, indexed by map id.
struct MapEntry {
  std::string filename;
  std::int8_t num8bppImages = 0;
  std::int8_t num4bppImages = 0;
  std::uint8_t flags = 0;      // see MapEntryFlags
  std::int8_t doorsId = 0;     // 1-based index into DOOR_MAPDATA, 0 = none
  std::int8_t toiletId = 0;    // 1-based index into TOILET_DATA, 0 = none
  std::int8_t loadingName = 0; // index into MAP_NAME_PTR

  bool operator==(const MapEntry&) const = default;
};

// The bits of MapEntry::flags (map.c: getMapSoundId, overworld.c: loadMapDigimon, updateTimeOfDay).
// 0x20 is not read by any known code.
struct MapEntryFlags {
  static constexpr std::uint8_t SoundIdMask = 0x1F;
  static constexpr std::uint8_t NoTimeOfDay = 0x40; // always the first palette of the background
  static constexpr std::uint8_t HasNpcs = 0x80;     // the entities section has NPC records
};

// map_object.h: DoorStruct. Six doors per entry; modelId -1 = none, otherwise DOOR\DOORnn.TMD.
struct DoorMapData {
  std::array<std::int8_t, 6> modelId{};
  std::array<std::int16_t, 6> posX{};
  std::array<std::int16_t, 6> posY{};
  std::array<std::int16_t, 6> posZ{};
  std::array<std::int16_t, 6> rotation{};

  bool operator==(const DoorMapData&) const = default;
};

// map.h: ToiletData. The Y fields are world Z.
struct ToiletData {
  std::int16_t posX1 = 0;
  std::int16_t posY1 = 0;
  std::int16_t posX2 = 0;
  std::int16_t posY2 = 0;

  bool operator==(const ToiletData&) const = default;
};

// An entry of the transport menu (script.h: BattleEntry, BIRDRA_TRANSPORT_TARGETS;
// shop/script_menu.c: tickBirdraTransport): where a pick goes, the trigger that lists it and what
// it costs.
struct TransportTarget {
  std::uint8_t mapId = 0;
  std::uint8_t exit = 0;
  std::uint16_t trigger = 0;
  std::uint32_t cost = 0;

  bool operator==(const TransportTarget&) const = default;
};

// map.h: MapLightUpdateData (utils2.c: updateMapLightState).
struct MapLightUpdateData {
  std::uint8_t mapId = 0;
  std::uint8_t mode = 0; // 0 = the objects are shown by day, 1 = by night
  std::int16_t startId = 0;
  std::int16_t count = 0;
  std::uint16_t trigger = 0; // 0xFFFF = always

  bool operator==(const MapLightUpdateData&) const = default;
};

// sound.h: MapSoundPara, a slice of SOUND/VHB/ESALL.VHB in 1 KB units, indexed by map sound id
// (MapEntryFlags::SoundIdMask). sound.c: loadMapSounds halves both fields to get sectors.
struct MapSoundPara {
  std::uint16_t sectorId = 0;
  std::uint16_t sectorCount = 0;

  bool operator==(const MapSoundPara&) const = default;
};

// An entry of the music list of the shop menu (MAIN_D_801303B8, shop/script_menu.c): the music font
// and SEQ that play for the BGM_TRACK_NAMES entry of the same index.
struct JukeboxTrack {
  std::uint8_t font = 0;
  std::uint8_t track = 0;

  bool operator==(const JukeboxTrack&) const = default;
};

// Every table of ExecutableLayout, read.
struct EvolutionChartDetail {
  std::array<std::array<std::int16_t, 8>, 20> lines{};
  std::array<std::array<std::uint8_t, 3>, 12> colors{};
  std::array<std::array<std::int16_t, 2>, 20> sprites{};
  std::array<std::array<std::int16_t, 4>, 6> insets{};
};

struct ExecutableTables {
  std::vector<DigimonPara> digimonData;
  std::vector<std::string> digimonFileNames;
  std::vector<std::vector<SkeletonBone>> digimonSkeletons;
  std::vector<Move> moveData;
  std::vector<std::string> moveNames;
  std::vector<std::array<std::uint8_t, 3>>
      moveLearnChances; // by learner special slot (battle_ui.c)
  std::vector<Item> itemPara;
  std::vector<std::string> itemDescriptions;
  // The palette of each item's icon in ETCDAT/ITEM.TIM: CLUT row 0x1E8 plus it (utils2.c:
  // setItemTexture).
  std::vector<std::uint8_t> itemCluts;
  std::vector<EvoRequirements> evolutionRequirements;
  std::vector<EvoStatsGains> evolutionStatsGains;
  std::vector<EvolutionPath> evolutionPaths;
  std::vector<RaiseData> raiseData;
  std::vector<EvolutionChartEntry> evolutionChart;
  std::optional<EvolutionChartDetail> evolutionChartDetail;
  std::vector<CardData> cardData;
  std::vector<std::int32_t> cardPrices;
  std::vector<std::uint8_t> cardIconPalettes;
  std::vector<std::string> tournamentNames;
  std::vector<std::string> tournamentGrades;
  std::vector<std::array<std::uint8_t, 6>>
      tournamentSchedule; // per day; 0xFF ends the day (dget.c)
  std::vector<std::uint16_t> tournamentMedalIds;
  std::vector<std::string> medalNames;
  std::vector<std::string> medalDescriptions;
  std::vector<std::string> bgmTrackNames;
  std::vector<std::uint8_t> enemyMusicMapping;
  MusicCode musicCode; // the music modes' and the battle's constants of the code
  std::vector<std::int16_t> shopMaps;
  std::vector<std::uint8_t> shopkeepDigimonTypes;
  std::vector<MapEntry> mapEntries;
  std::vector<std::string> mapNames;
  std::vector<DoorMapData> doorMapData;
  std::vector<ToiletData> toiletData;
  std::vector<MapLightUpdateData> mapLightUpdateData;
  std::vector<std::uint8_t>
      digimonVlallSoundIds; // VLALL entry by partner type (sound.c: loadPartnerSounds)
  std::vector<std::uint8_t>
      digimonVballSoundIds; // VBALL entry by type (sound.c: loadDigimonSounds)
  std::vector<MapSoundPara> mapSoundPara;
  std::vector<JukeboxTrack> jukeboxTracks;
  std::vector<ItemEffectCode> itemEffects; // one per item of itemPara
  std::vector<TransportTarget> transportTargets;
  // Where a type's condition effects hang (effect_common.c: translateConditionFXToEntity): an
  // offset x, y, z in a node's space and the node (pad).
  std::vector<std::array<std::int16_t, 4>> conditionFxOffsets;
};

// Reads a table of char* pointers to NUL-terminated strings. A null pointer reads as an empty
// string.
core::Result<std::vector<std::string>> readStringTable(const MemoryImage& image,
                                                       const TableLocation& table);

core::Result<std::vector<DigimonPara>> readDigimonData(const MemoryImage& image,
                                                       const ExecutableLayout& layout);

// Reads the bone list of each Digimon type; DIGIMON_DATA gives how many bones each list has.
core::Result<std::vector<std::vector<SkeletonBone>>>
readDigimonSkeletons(const MemoryImage& image, const ExecutableLayout& layout,
                     const std::vector<DigimonPara>& digimonData);

core::Result<std::vector<Move>> readMoveData(const MemoryImage& image,
                                             const ExecutableLayout& layout);
core::Result<std::vector<Item>> readItemPara(const MemoryImage& image,
                                             const ExecutableLayout& layout);
core::Result<std::vector<EvoRequirements>>
readEvolutionRequirements(const MemoryImage& image, const ExecutableLayout& layout);
core::Result<std::vector<EvoStatsGains>> readEvolutionStatsGains(const MemoryImage& image,
                                                                 const ExecutableLayout& layout);
core::Result<std::vector<EvolutionPath>> readEvolutionPaths(const MemoryImage& image,
                                                            const ExecutableLayout& layout);
core::Result<std::vector<RaiseData>> readRaiseData(const MemoryImage& image,
                                                   const ExecutableLayout& layout);
core::Result<std::vector<EvolutionChartEntry>> readEvolutionChart(const MemoryImage& image,
                                                                  const ExecutableLayout& layout);
core::Result<std::vector<CardData>> readCardData(const MemoryImage& image,
                                                 const ExecutableLayout& layout);
core::Result<std::vector<MapEntry>> readMapEntries(const MemoryImage& image,
                                                   const ExecutableLayout& layout);
core::Result<std::vector<DoorMapData>> readDoorMapData(const MemoryImage& image,
                                                       const ExecutableLayout& layout);
core::Result<std::vector<ToiletData>> readToiletData(const MemoryImage& image,
                                                     const ExecutableLayout& layout);
core::Result<std::vector<MapLightUpdateData>>
readMapLightUpdateData(const MemoryImage& image, const ExecutableLayout& layout);
core::Result<std::vector<MapSoundPara>> readMapSoundPara(const MemoryImage& image,
                                                         const ExecutableLayout& layout);
core::Result<std::vector<JukeboxTrack>> readJukeboxTracks(const MemoryImage& image,
                                                          const ExecutableLayout& layout);

// Reads every table of the layout. Fails on the first table that is not located or does not fit in
// the image.
core::Result<ExecutableTables> readExecutableTables(const MemoryImage& image,
                                                    const ExecutableLayout& layout);

} // namespace dwe::formats::exe
