#include <dwe/game/partner/messages.hpp>
#include <dwe/game/partner/species.hpp>
#include <dwe/ruleset/species_extra.hpp>

#include <functional>
#include <map>
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
  if (const auto found = species_.find(id); found != species_.end()) {
    return &found->second;
  }
  const auto borrowed = borrowed_.find(id);
  return borrowed == borrowed_.end() ? nullptr : &borrowed->second;
}

void SpeciesTable::add(SpeciesCare species) {
  std::string id = species.id;
  species_.insert_or_assign(std::move(id), std::move(species));
}

void SpeciesTable::addBorrowed(SpeciesCare species) {
  std::string id = species.id;
  borrowed_.insert_or_assign(std::move(id), std::move(species));
}

core::Result<SpeciesTable> buildSpeciesTable(const assets::DigimonTable& digimon,
                                             const assets::RaiseTable& raise) {
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
  // The developer's evolve makes any species with a model the partner, the enemies' and the
  // characters' too: those with no raise row care as their stand-in does.
  std::map<std::string, const assets::RaiseRow*, std::less<>> raiseOf;
  for (const assets::RaiseRow& row : raise.raise) {
    if (row.digimon) {
      raiseOf.insert_or_assign(*row.digimon, &row);
    }
  }
  const auto hasRaise = [&](const assets::DigimonRow& row) { return raiseOf.contains(row.id); };
  for (const assets::DigimonRow& row : digimon.digimon) {
    if (row.boneCount <= 0 || hasRaise(row)) {
      continue;
    }
    if (const assets::DigimonRow* standIn = assets::standInRow(digimon, row, hasRaise);
        standIn != nullptr) {
      table.addBorrowed(careOf(*raiseOf.at(standIn->id), row));
    }
  }
  return table;
}

core::Result<SpeciesTable> loadSpeciesTable(const assets::VirtualFileSystem& vfs,
                                            const ruleset::Ruleset& ruleset) {
  if (ruleset.table(DigimonTableRole) == nullptr) {
    return core::fail(formatMessage(messages::NoTableRole, ruleset.id(), DigimonTableRole));
  }
  if (ruleset.table(RaiseTableRole) == nullptr) {
    return core::fail(formatMessage(messages::NoTableRole, ruleset.id(), RaiseTableRole));
  }
  // With the ruleset's species patches (the hidden content's raise rows and moves).
  auto digimon = ruleset::loadPatchedDigimonTable(vfs, ruleset);
  if (!digimon) {
    return std::unexpected{digimon.error()};
  }
  auto raise = ruleset::loadPatchedRaiseTable(vfs, ruleset);
  if (!raise) {
    return std::unexpected{raise.error()};
  }
  return buildSpeciesTable(*digimon, *raise);
}

} // namespace dwe::game::partner
