#include <dwe/formats/exe/executable_layout.hpp>

#include <array>

namespace dwe::formats::exe {

namespace {

// A version the tools know whose tables have not been located yet.
constexpr ExecutableLayout unlocated(std::string_view version, bool inlineNames) {
    ExecutableLayout layout;
    layout.version = version;
    layout.inlineNames = inlineNames;
    return layout;
}

// US addresses: dw_decomp config/us/symbols.txt. Counts: the array declarations (evolution.c, partner.h, model.c,
// map_object.h, tournament.c) or, where there is none, the symbol's size divided by the struct size. EU and JP are
// known but not located yet; their readers fail until someone fills them from config/<version>/symbols.txt.
constexpr std::array Layouts = {
    ExecutableLayout{
        .version = "us",
        .inlineNames = true,
        .digimonData = {.address = 0x8012CEB4, .count = 180},
        .digimonFileNames = {.address = 0x8011D19C, .count = 180},
        .digimonSkeletons = {.address = 0x8011CE60, .count = 180},
        .moveData = {.address = 0x8012623C, .count = 122},
        .moveNames = {.address = 0x80126054, .count = 122},
        .itemPara = {.address = 0x801269DC, .count = 128},
        .itemDescriptions = {.address = 0x801279DC, .count = 128},
        .evolutionRequirements = {.address = 0x8012ABEC, .count = 63},
        .evolutionStatsGains = {.address = 0x8012B2D0, .count = 66},
        .evolutionPaths = {.address = 0x8012B66C, .count = 62},
        .raiseData = {.address = 0x801225BC, .count = 66},
        .cardData = {.address = 0x8012FFD8, .count = 66},
        .cardPrices = {.address = 0x8012FFC4, .count = 5},
        .tournamentNames = {.address = 0x801300E0, .count = 23},
        .tournamentGrades = {.address = 0x8013013C, .count = 23},
        .tournamentSchedule = {.address = 0x80130198, .count = 30},
        .tournamentMedalIds = {.address = 0x8012BACC, .count = 6},
        .medalNames = {.address = 0x80124828, .count = 15},
        .medalDescriptions = {.address = 0x80124864, .count = 45},
        .bgmTrackNames = {.address = 0x8012FEC8, .count = 63},
        .enemyMusicMapping = {.address = 0x8012BAE4, .count = 48},
        .shopMaps = {.address = 0x8012FE68, .count = 8},
        .shopkeepDigimonTypes = {.address = 0x8012FE5C, .count = 11},
        .mapEntries = {.address = 0x801292D4, .count = 255},
        .mapNames = {.address = 0x801291BC, .count = 70},
        .doorMapData = {.address = 0x80122E60, .count = 24},
        .toiletData = {.address = 0x80122E10, .count = 10},
        .mapLightUpdateData = {.address = 0x801345C4, .count = 1},
    },
    unlocated("eu", false),
    unlocated("jp", true),
    unlocated("jp_rev1", true),
};

} // namespace

std::span<const ExecutableLayout> executableLayouts() {
    return Layouts;
}

std::optional<ExecutableLayout> findExecutableLayout(std::string_view version) {
    for (const auto& layout : Layouts) {
        if (layout.version == version) {
            return layout;
        }
    }
    return std::nullopt;
}

} // namespace dwe::formats::exe
