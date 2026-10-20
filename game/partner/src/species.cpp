#include <dwe/game/partner/messages.hpp>
#include <dwe/game/partner/species.hpp>

#include <string>
#include <utility>

namespace dwe::game::partner {

namespace {

using messages::formatMessage;

constexpr auto HighestLevel = static_cast<std::int32_t>(Level::Ultimate);

SpeciesCare careOf(const assets::RaiseRow& raise, const assets::DigimonRow& digimon) {
    const std::int32_t level = digimon.level < 0 || digimon.level > HighestLevel ? 0 : digimon.level;
    return {.id = digimon.id,
            .level = static_cast<Level>(level),
            .hungerHours = raise.hungerTimes,
            .energyCap = raise.energyCap,
            .energyThreshold = raise.energyThreshold,
            .energyUsage = raise.energyUsage,
            .poopInterval = raise.poopTimer,
            .poopSize = raise.poopSize,
            .favoriteFood = raise.favoriteFood,
            .sleepPattern = raise.sleepCycle,
            .favoredRegion = raise.favoredRegion,
            .trainingAptitude = raise.trainingType,
            .defaultWeight = raise.defaultWeight};
}

} // namespace

const SpeciesCare* SpeciesTable::find(std::string_view id) const {
    const auto found = species_.find(id);
    return found == species_.end() ? nullptr : &found->second;
}

void SpeciesTable::add(SpeciesCare species) {
    std::string id = species.id;
    species_.insert_or_assign(std::move(id), std::move(species));
}

core::Result<SpeciesTable> buildSpeciesTable(const assets::DigimonTable& digimon, const assets::RaiseTable& raise) {
    SpeciesTable table;
    for (const assets::RaiseRow& row : raise.raise) {
        if (!row.digimon) {
            continue;
        }
        const assets::DigimonRow* digimonRow = digimon.find(*row.digimon);
        if (digimonRow == nullptr) {
            return core::fail(formatMessage(messages::RaiseWithoutDigimon, *row.digimon));
        }
        table.add(careOf(row, *digimonRow));
    }
    return table;
}

core::Result<SpeciesTable> loadSpeciesTable(const assets::VirtualFileSystem& vfs, const ruleset::Ruleset& ruleset) {
    const assets::AssetId* digimonId = ruleset.table(DigimonTableRole);
    if (digimonId == nullptr) {
        return core::fail(formatMessage(messages::NoTableRole, ruleset.id(), DigimonTableRole));
    }
    const assets::AssetId* raiseId = ruleset.table(RaiseTableRole);
    if (raiseId == nullptr) {
        return core::fail(formatMessage(messages::NoTableRole, ruleset.id(), RaiseTableRole));
    }
    auto digimon = assets::loadDigimonTable(vfs, *digimonId);
    if (!digimon) {
        return std::unexpected{digimon.error()};
    }
    auto raise = assets::loadRaiseTable(vfs, *raiseId);
    if (!raise) {
        return std::unexpected{raise.error()};
    }
    return buildSpeciesTable(*digimon, *raise);
}

} // namespace dwe::game::partner
