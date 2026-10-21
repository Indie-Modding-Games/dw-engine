#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/table_composition.hpp>
#include <dwe/core/error.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// The care parameters of each partner Digimon, `data/raise` (content/schemas/raise.schema.json): the executable's
// RAISE_DATA (partner.h: RaiseData). Numbers are kept as the extractor wrote them.
struct RaiseRow {
    // The Digimon id (digimon.yaml), or the raw number as text.
    std::optional<std::string> digimon;
    // The Digimon's place in the original; none for a pack's new row (TableComposition: unnumbered).
    std::optional<std::int32_t> index;
    // The hours the partner gets hungry, from rookie on; -1 ends the list.
    std::array<std::int32_t, 8> hungerTimes{};
    std::int32_t energyCap = 0;
    std::int32_t energyThreshold = 0;
    std::int32_t energyUsage = 0;
    // Steps of 10 game minutes between two visits to the toilet.
    std::int32_t poopTimer = 0;
    std::int32_t unknown0e = 0;
    std::int32_t poopSize = 0;
    // The item id (items.yaml), or the raw number as text.
    std::optional<std::string> favoriteFood;
    // Index into the sleep patterns of the ruleset's care section.
    std::int32_t sleepCycle = 0;
    std::int32_t favoredRegion = 0;
    // The aptitude for each kind of training (trn_reward.c: TRN_calculateTrainingMultiplier).
    std::int32_t trainingType = 0;
    std::int32_t defaultWeight = 0;
    std::array<std::int32_t, 3> view{};
};

struct RaiseTable {
    std::vector<RaiseRow> raise;

    // The row of a Digimon id, or nullptr.
    [[nodiscard]] const RaiseRow* find(std::string_view digimon) const;
};

// How the packs' files of the table compose: rows by their Digimon's id (table_composition.hpp).
inline constexpr std::array<EntrySection, 1> RaiseEntrySections{
    {{.section = "raise", .entryKey = "digimon", .numbered = false, .ownsIds = false}}};
inline constexpr TableComposition RaiseComposition{.sections = RaiseEntrySections};

// Parses the text of `data/raise`. Unknown and missing keys are errors.
core::Result<RaiseTable> parseRaiseTable(std::string_view yaml);

// Reads and parses the raise table at `id` (a ruleset names it by role) from the virtual file system.
core::Result<RaiseTable> loadRaiseTable(const VirtualFileSystem& vfs, const AssetId& id);

} // namespace dwe::assets
