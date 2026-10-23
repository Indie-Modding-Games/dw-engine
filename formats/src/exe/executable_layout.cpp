#include <dwe/formats/exe/executable_layout.hpp>
#include <dwe/formats/exe/item_effects.hpp>

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

using Reg = MipsRegister;

// Where handleFood keeps each local when its cases join (US build: the arguments of addEnergy,
// reduceTiredness, addHappiness, addDiscipline, addWeight, setTrainingBoost, handleItemSickness and
// the addWithLimit calls after the switch), in FoodValue order.
constexpr std::array<ValueLocation, FoodValueCount> UsFoodValues{
    ValueLocation::inRegister(Reg::V0), ValueLocation::inRegister(Reg::S2),
    ValueLocation::inRegister(Reg::S0), ValueLocation::onStack(56),
    ValueLocation::inRegister(Reg::S1), ValueLocation::onStack(60),
    ValueLocation::inRegister(Reg::S8), ValueLocation::inRegister(Reg::S7),
    ValueLocation::inRegister(Reg::S5), ValueLocation::inRegister(Reg::S4),
    ValueLocation::onStack(64),         ValueLocation::onStack(68),
    ValueLocation::onStack(72),         ValueLocation::onStack(76),
    ValueLocation::onStack(80),         ValueLocation::onStack(84),
    ValueLocation::inRegister(Reg::S6), ValueLocation::inRegister(Reg::S3),
};

// Where handleChips keeps each local when its cases join (US build: the addWithLimit and
// modifyLifetime calls after the switch), in ChipValue order.
constexpr std::array<ValueLocation, ChipValueCount> UsChipValues{
    ValueLocation::inRegister(Reg::S1), ValueLocation::inRegister(Reg::S3),
    ValueLocation::inRegister(Reg::S2), ValueLocation::inRegister(Reg::S4),
    ValueLocation::inRegister(Reg::S5), ValueLocation::inRegister(Reg::S6),
    ValueLocation::inRegister(Reg::S0),
};

// handleMusicOverride (US build) stores the font through s1 and the song through s0, the pointers
// its caller passed.
constexpr std::array UsMusicOverrideValues{
    ValueLocation::pointedBy(Reg::S1),
    ValueLocation::pointedBy(Reg::S0),
};

// US addresses: dw_decomp config/us/symbols.txt. Counts: the array declarations (evolution.c,
// partner.h, model.c, map_object.h, tournament.c, sound.c, shop/script_menu.c) or, where there is
// none, the symbol's size divided by the struct size. EU and JP are known but not located yet;
// their readers fail until someone fills them from config/<version>/symbols.txt.
constexpr std::array Layouts = {
    ExecutableLayout{
        .version = "us",
        .inlineNames = true,
        .digimonData = {.address = 0x8012CEB4, .count = 180},
        .digimonFileNames = {.address = 0x8011D19C, .count = 180},
        .digimonSkeletons = {.address = 0x8011CE60, .count = 180},
        .moveData = {.address = 0x8012623C, .count = 122},
        .moveNames = {.address = 0x80126054, .count = 122},
        .moveLearnChances = {.address = 0x80125FA4, .count = 58},
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
        .digimonVlallSoundIds = {.address = 0x80127EFC, .count = 66},
        .digimonVballSoundIds = {.address = 0x80127F40, .count = 180},
        .mapSoundPara = {.address = 0x80127FF4, .count = 22},
        .jukeboxTracks = {.address = 0x801303B8, .count = 64},
        .itemFunctions = {.address = 0x80127C88, .count = 128},
        .healingValues = {.address = 0x8013435C, .count = 4},
        .evolutionItemTargets = {.address = 0x80127C5C, .count = 44},
        .transportTargets = {.address = 0x8013024C, .count = 6},
        .itemCode =
            {
                .handlers = {.hpHealing = 0x800C4AC0,
                             .mpHealing = 0x800C4A08,
                             .doubleFloppy = 0x800C4950,
                             .status = 0x800C4728,
                             .revive = 0x800C4834,
                             .battleDisk = 0x800F06F0,
                             .chip = 0x800C44C0,
                             .food = 0x800C396C,
                             .evolution = 0x800C3860},
                .firstHpHealingItem = 0,
                .firstMpHealingItem = 4,
                .firstEvolutionItem = 0x47,
                // The joins are the first instruction after the switches of the compiled functions.
                .food = {.function = 0x800C396C, .join = 0x800C4314, .values = UsFoodValues},
                .chips = {.function = 0x800C44C0, .join = 0x800C4658, .values = UsChipValues},
            },
        .musicCode =
            {
                // The run starts after the readPStat(0xF5) call, with its result in v0; the join is
                // the function's epilogue.
                .fieldOverrides = {.function = 0x80105578,
                                   .join = 0x80105688,
                                   .values = UsMusicOverrideValues,
                                   .argument = Reg::V0},
                .firstOverrideMode = 2,
                .lastOverrideMode = 10,
                .battle = {.font = 0x800E7520,
                           .bossType = 0x800E750C,
                           .bossTrack = 0x800E7528,
                           .scriptedTrack = 0x800E7564,
                           .defaultTrack = 0x800E75CC,
                           .firstEnemyType = 0x800E7588,
                           .endEnemyType = 0x800E7594},
            },
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
