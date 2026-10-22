#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/table_composition.hpp>

#include <span>
#include <string_view>
#include <utility>
#include <vector>

// Which assets of the packs are content tables that compose entry by entry, and of which kind, so
// that every reader of the packs (the game's loaders, checkContent and dwx validate) composes the
// same ids (docs/architecture/vfs.md#tablas-por-entradas).
namespace dwe::assets {

class VirtualFileSystem;

// Where a pack keeps its rulesets and the key a ruleset's manifest names its tables by role under:
// the part of the ruleset format (docs/architecture/rulesets.md) that the asset layer reads, since
// dwx validate sees no ruleset module. engine/ruleset reads the rest of the format and takes these
// from here.
inline constexpr std::string_view RulesetsFolder = "rules";
inline constexpr std::string_view RulesetManifestLeaf = "ruleset";
inline constexpr std::string_view RulesetTablesKey = "tables";

// A kind of content table: the role a ruleset names a table of the kind by, the id dwx extracts it
// to, and how its packs' files compose.
struct TableKind {
  std::string_view role;
  std::string_view defaultId;
  const TableComposition* composition = nullptr;
};

// Every kind of content table: digimon, raise, maps, items, moves and evolution.
std::span<const TableKind> composedTableKinds();

// The content tables of the packs with their kind: the default id of each kind that a pack has, and
// every table that a ruleset of the packs (`rules/<id>/ruleset`, its `tables`) names for a kind's
// role. A ruleset that cannot be read names none here; its own check reports it.
std::vector<std::pair<AssetId, const TableKind*>> composedTablesOf(const VirtualFileSystem& vfs);

} // namespace dwe::assets
