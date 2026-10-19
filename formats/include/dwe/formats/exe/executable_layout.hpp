#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace dwe::formats::exe {

// Where a table of the executable lives: its memory address and how many entries it has. An address of 0 means the
// table has not been located for that version yet.
struct TableLocation {
    std::uint32_t address = 0;
    std::uint32_t count = 0;

    [[nodiscard]] bool located() const { return address != 0; }
};

// The tables of one version's main executable. Addresses and counts come from the decomp
// (config/<version>/symbols.txt and the array declarations in src/main); the data itself is never stored here.
struct ExecutableLayout {
    std::string_view version; // the version id of disc/known_releases: us, eu, jp, jp_rev1

    // DigimonPara and Item start with an inline char name[20] (params.h, item.h); the EU release drops it.
    bool inlineNames = true;

    TableLocation digimonData;           // DigimonPara[]
    TableLocation digimonFileNames;      // char*[]: PTR_DIGIMON_FILE_NAMES
    TableLocation digimonSkeletons;      // SkeletonBone*[], bone count from DigimonPara
    TableLocation moveData;              // Move[]
    TableLocation moveNames;             // char*[]
    TableLocation itemPara;              // Item[]
    TableLocation itemDescriptions;      // char*[]: ITEM_DESCS
    TableLocation evolutionRequirements; // EvoRequirements[]
    TableLocation evolutionStatsGains;   // EvoStatsGains[]
    TableLocation evolutionPaths;        // EvolutionPath[], entry i is Digimon i + 1
    TableLocation raiseData;             // RaiseData[]
    TableLocation cardData;              // CardData[]
    TableLocation cardPrices;            // int32[]
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
};

// Returns every version the tools know, located or not.
std::span<const ExecutableLayout> executableLayouts();

// Returns the layout of a version, if the tools know it.
std::optional<ExecutableLayout> findExecutableLayout(std::string_view version);

} // namespace dwe::formats::exe
