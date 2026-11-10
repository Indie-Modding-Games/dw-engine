#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/table_composition.hpp>
#include <dwe/core/error.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// Where a species' condition effects hang (effect_common.c: CONDITION_FX_OFFSETS,
// translateConditionFXToEntity): an offset in the space of one node of its model, in the original's
// axes and units.
struct ConditionFxPoint {
  std::uint32_t node = 0;
  std::array<std::int32_t, 3> offset{};
};

// The table of every Digimon, `data/digimon` (content/schemas/digimon.schema.json): the
// executable's DIGIMON_DATA (params.h: DigimonPara). References to other tables are their ids, or
// the raw number as text when the extractor found it outside its table.
struct DigimonRow {
  std::string id;
  // The original's Digimon id, which the other tables and the code use.
  std::int32_t index = 0;
  std::string name;
  std::string fileName;
  std::int32_t boneCount = 0;
  std::int32_t radius = 0;
  std::int32_t height = 0;
  std::int32_t type = 0;
  // 1 baby, 2 in-training, 3 rookie, 4 champion, 5 ultimate; 0 for the entries that are no partner.
  std::int32_t level = 0;
  std::array<std::optional<std::int32_t>, 3> special{};
  std::optional<std::string> dropItem;
  std::int32_t dropChance = 0;
  std::array<std::optional<std::string>, 16> moves{};
  // Nothing for a species the executable's table does not cover (a pack's new one).
  std::optional<ConditionFxPoint> conditionFx;
};

struct DigimonTable {
  std::vector<DigimonRow> digimon;

  // The row with this id, or nullptr.
  [[nodiscard]] const DigimonRow* find(std::string_view id) const;
};

// The level of the rookies (DigimonRow::level).
inline constexpr std::int32_t RookieLevel = 3;

// The species whose rows of another table (care, evolution stats) stand in for one that has none,
// for the developer's evolve into a species only enemies or characters use: the first row with
// rows (`hasRows`) among those of the same name (an enemy's copy of a partner form), then of the
// same level and type, then of the same level, then the rookies. nullptr when none has rows; the
// species itself when it has its own.
[[nodiscard]] const DigimonRow* standInRow(const DigimonTable& table, const DigimonRow& species,
                                           const std::function<bool(const DigimonRow&)>& hasRows);

// Whether a species' row lists any move: the enemies' copies of the partner forms list none (they
// fight with their map's moves), so the developer's evolve gives them their stand-in's list.
[[nodiscard]] bool listsMoves(const DigimonRow& species);

// How the packs' files of the table compose: rows by id (table_composition.hpp).
inline constexpr std::array<EntrySection, 1> DigimonEntrySections{
    {{.section = "digimon", .entryKey = "id"}}};
inline constexpr TableComposition DigimonComposition{.sections = DigimonEntrySections};

// Parses the text of `data/digimon`. Unknown and missing keys are errors.
core::Result<DigimonTable> parseDigimonTable(std::string_view yaml);

// The folder of the species' assets, `digimon/<folder of the id>/` (contentAssetPath), and the leaf
// of their model contract.
inline constexpr std::string_view SpeciesFolder = "digimon";
inline constexpr std::string_view SpeciesContractLeaf = "contract";

// The id of a species' model contract (content/schemas/model_contract.schema.json): what a model
// must have to be that species', `digimon/<folder>/contract`.
std::string speciesContractId(std::string_view species);

// Checks that every Digimon a pack adds (a namespaced id) has its model contract in the packs, so
// that its model is checked against it: an original's model is checked against the original's, a
// new one has no other.
core::Result<void> requireNewSpeciesContracts(const VirtualFileSystem& vfs,
                                              const DigimonTable& table);

// Reads and parses the digimon table at `id` (a ruleset names it by role) from the virtual file
// system, and checks the contracts of the new Digimon (requireNewSpeciesContracts).
core::Result<DigimonTable> loadDigimonTable(const VirtualFileSystem& vfs, const AssetId& id);

} // namespace dwe::assets
