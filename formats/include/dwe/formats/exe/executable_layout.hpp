#pragma once

#include <dwe/formats/exe/constant_switch.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace dwe::formats::exe {

// Where a table of the executable lives: its memory address and how many entries it has. An address
// of 0 means the table has not been located for that version yet.
struct TableLocation {
  std::uint32_t address = 0;
  std::uint32_t count = 0;

  [[nodiscard]] bool located() const { return address != 0; }
};

// The functions ITEM_FUNCTIONS points to (item.c; main.c: handleBuffDisks).
struct ItemHandlerAddresses {
  std::uint32_t hpHealing = 0;    // handleHPHealingItem
  std::uint32_t mpHealing = 0;    // handleMPHealingItem
  std::uint32_t doubleFloppy = 0; // handleDoubleFloppy
  std::uint32_t status = 0;       // handleStatusItems
  std::uint32_t revive = 0;       // handleRestore
  std::uint32_t battleDisk = 0;   // handleBuffDisks
  std::uint32_t chip = 0;         // handleChips
  std::uint32_t food = 0;         // handleFood
  std::uint32_t evolution = 0;    // handleEvoItems
};

// The item code of a version: the handlers, the first item each per-item table is indexed from, and
// the constant switches whose cases hold the values of food and chips (exe/item_effects.hpp).
struct ItemCodeLayout {
  ItemHandlerAddresses handlers;
  std::uint8_t firstHpHealingItem = 0; // handleHPHealingItem reads HEALING_VALUES[item - this]
  std::uint8_t firstMpHealingItem = 0; // handleMPHealingItem reads HEALING_VALUES[item - this]
  std::uint8_t firstEvolutionItem = 0; // handleEvoItems reads EVOLUTION_ITEM_TARGET[item - this]
  ConstantSwitch food;                 // handleFood, values in FoodValue order
  ConstantSwitch chips;                // handleChips, values in ChipValue order
};

// The instructions whose constants say which song a battle plays (btl.c: loadBattleData), each the
// address of one `addiu`, `slti` or `addu` (exe/music_code.hpp).
struct BattleMusicCodeLayout {
  std::uint32_t font = 0;           // playMusic(0x21, ..): the battle font
  std::uint32_t bossType = 0;       // the first enemy's type that plays bossTrack
  std::uint32_t bossTrack = 0;      // playMusic(.., 3)
  std::uint32_t scriptedTrack = 0;  // IS_PREDEFINED_BATTLE: playMusic(.., 0)
  std::uint32_t defaultTrack = 0;   // other enemies: playMusic(.., 1)
  std::uint32_t firstEnemyType = 0; // type >= this reads ENEMY_MUSIC_MAPPING[type - this]
  std::uint32_t endEnemyType = 0;   // type < this

  [[nodiscard]] bool located() const { return font != 0; }
};

// The music the code picks with constants: the fixed font and song of the music modes of
// handleMusicOverride (script_interp.c), a switch that stores them through its two pointers, and
// the battle music.
struct MusicCodeLayout {
  ConstantSwitch fieldOverrides; // values: the font, then the song
  std::uint8_t firstOverrideMode = 0;
  std::uint8_t lastOverrideMode = 0;
  BattleMusicCodeLayout battle;
};

// The tables of one version's main executable. Addresses and counts come from the decomp
// (config/<version>/symbols.txt and the array declarations in src/main); the data itself is never
// stored here.
struct ExecutableLayout {
  std::string_view version; // the version id of disc/known_releases: us, eu, jp, jp_rev1

  // DigimonPara and Item start with an inline char name[20] (params.h, item.h); the EU release
  // drops it.
  bool inlineNames = true;

  TableLocation digimonData;           // DigimonPara[]
  TableLocation digimonFileNames;      // char*[]: PTR_DIGIMON_FILE_NAMES
  TableLocation digimonSkeletons;      // SkeletonBone*[], bone count from DigimonPara
  TableLocation moveData;              // Move[]
  TableLocation moveNames;             // char*[]
  TableLocation moveLearnChances;      // u8[3] per learnable move: MOVE_LEARN_CHANCES
  TableLocation itemPara;              // Item[]
  TableLocation itemDescriptions;      // char*[]: ITEM_DESCS
  TableLocation itemCluts;             // u8[]: ITEM_CLUT_DATA, the icon's palette
  TableLocation evolutionRequirements; // EvoRequirements[]
  TableLocation evolutionStatsGains;   // EvoStatsGains[]
  TableLocation evolutionPaths;        // EvolutionPath[], entry i is Digimon i + 1
  TableLocation raiseData;             // RaiseData[]
  TableLocation evolutionChart;        // EvoChartEntry[]: layout and icon UVs, not executable code
  TableLocation evolutionChartDetail;  // Line, sprite, color and inset records for Chart detail
  TableLocation cardData;              // CardData[]
  TableLocation cardPrices;            // int32[]
  TableLocation cardIconPalettes;      // u8[]: CARD_SPRITE_CLUT, the card icon's palette
  TableLocation tournamentNames;       // char*[]
  TableLocation tournamentGrades;      // char*[]
  TableLocation tournamentSchedule;    // u8[6] per day
  TableLocation tournamentMedalIds;    // u16[]
  TableLocation medalNames;            // char*[]
  TableLocation medalDescriptions;     // char*[], three lines per medal
  TableLocation bgmTrackNames;         // char*[]
  TableLocation enemyMusicMapping;     // u8[]
  TableLocation shopMaps;              // int16[]
  TableLocation shopkeepDigimonTypes;  // u8[], 0xFF ends the list
  TableLocation mapEntries;            // MapEntry[]
  TableLocation mapNames;              // char*[]: MAP_NAME_PTR
  TableLocation doorMapData;           // DoorStruct[]
  TableLocation toiletData;            // ToiletData[]
  TableLocation mapLightUpdateData;    // MapLightUpdateData[]
  TableLocation digimonVlallSoundIds;  // u8[]: DIGIMON_VLALL_SOUND_ID
  TableLocation digimonVballSoundIds;  // u8[]: DIGIMON_VBALL_SOUND_ID
  TableLocation mapSoundPara;          // MapSoundPara[]: MAP_SOUND_PARA
  TableLocation jukeboxTracks;         // u8[2][]: MAIN_D_801303B8
  TableLocation itemFunctions;         // ItemFunction[]: ITEM_FUNCTIONS, one per item
  TableLocation healingValues;         // int16[]: HEALING_VALUES
  TableLocation evolutionItemTargets;  // u8[]: EVOLUTION_ITEM_TARGET
  TableLocation transportTargets;      // TransportTarget[]: BIRDRA_TRANSPORT_TARGETS
  TableLocation conditionFxOffsets;    // SVECTOR[]: CONDITION_FX_OFFSETS, by original type
  ItemCodeLayout itemCode;
  MusicCodeLayout musicCode;
};

// Returns every version the tools know, located or not.
std::span<const ExecutableLayout> executableLayouts();

// Returns the layout of a version, if the tools know it.
std::optional<ExecutableLayout> findExecutableLayout(std::string_view version);

} // namespace dwe::formats::exe
