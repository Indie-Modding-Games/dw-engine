#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/raise_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::game::partner {

// The levels of evolution (params.h: DigimonPara.level).
enum class Level : std::uint8_t { None, Baby, InTraining, Rookie, Champion, Ultimate };

// The care data of one partner species: its row of the raise table and its level (raise.yaml,
// digimon.yaml).
struct SpeciesCare {
  std::string id;
  Level level = Level::None;
  // The hours it gets hungry from rookie on; -1 ends the list (RaiseData.hungerTimes).
  std::array<std::int32_t, 8> hungerHours{};
  std::int32_t energyCap = 0;
  std::int32_t energyThreshold = 0;
  std::int32_t energyUsage = 0;
  // Steps between two needs of the toilet (poopTimer).
  std::int32_t poopInterval = 0;
  std::int32_t poopSize = 0;
  std::optional<std::string> favoriteFood;
  // Index into the care section's sleep patterns.
  std::int32_t sleepPattern = 0;
  std::int32_t favoredRegion = 0;
  // The tier of TrainingRules::aptitudeTenths.
  std::int32_t trainingAptitude = 0;
  std::int32_t defaultWeight = 0;
};

// The care data of every species that has a raise row.
class SpeciesTable {
public:
  // The species with this Digimon id, its own row or a borrowed one, or nullptr.
  [[nodiscard]] const SpeciesCare* find(std::string_view id) const;

  // The species with their own raise rows.
  [[nodiscard]] const std::map<std::string, SpeciesCare, std::less<>>& species() const {
    return species_;
  }

  void add(SpeciesCare species);
  // The care of a species with no raise row, borrowed from another's (assets::standInRow), so that
  // the developer's evolve can make it the partner. Its own row, added later, wins.
  void addBorrowed(SpeciesCare species);

private:
  std::map<std::string, SpeciesCare, std::less<>> species_;
  std::map<std::string, SpeciesCare, std::less<>> borrowed_;
};

// The table roles a ruleset names for the species data (rules/<id>/ruleset.yaml: tables).
inline constexpr std::string_view DigimonTableRole = "digimon";
inline constexpr std::string_view RaiseTableRole = "raise";

// Joins the raise rows with the level of their Digimon. A raise row whose Digimon is not in the
// table is an error. Every other species with a model borrows the raise row of its stand-in
// (assets::standInRow) with its own id and level.
core::Result<SpeciesTable> buildSpeciesTable(const assets::DigimonTable& digimon,
                                             const assets::RaiseTable& raise);

// Reads the digimon and raise tables a ruleset names and joins them.
core::Result<SpeciesTable> loadSpeciesTable(const assets::VirtualFileSystem& vfs,
                                            const ruleset::Ruleset& ruleset);

} // namespace dwe::game::partner
