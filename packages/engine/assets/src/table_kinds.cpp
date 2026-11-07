#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/evolution_table.hpp>
#include <dwe/assets/item_effect_table.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/assets/move_table.hpp>
#include <dwe/assets/raise_table.hpp>
#include <dwe/assets/table_kinds.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <utility>

namespace dwe::assets {

namespace {

constexpr std::array<TableKind, 7> Kinds{{
    {.role = "digimon", .defaultId = "data/digimon", .composition = &DigimonComposition},
    {.role = "raise", .defaultId = "data/raise", .composition = &RaiseComposition},
    {.role = "maps", .defaultId = "data/maps", .composition = &MapComposition},
    {.role = "items", .defaultId = "data/items", .composition = &ItemComposition},
    {.role = "item_effects",
     .defaultId = "data/item_effects",
     .composition = &ItemEffectComposition},
    {.role = "moves", .defaultId = "data/moves", .composition = &MoveComposition},
    {.role = "evolution", .defaultId = "data/evolution", .composition = &EvolutionComposition},
}};

const TableKind* kindOfRole(std::string_view role) {
  const auto found = std::ranges::find(Kinds, role, &TableKind::role);
  return found == Kinds.end() ? nullptr : &*found;
}

// Adds a table unless it is listed already: a kind's default id is also what the rulesets name.
void addTable(std::vector<std::pair<AssetId, const TableKind*>>& tables, AssetId id,
              const TableKind& kind) {
  if (std::ranges::none_of(tables, [&](const auto& table) { return table.first == id; })) {
    tables.emplace_back(std::move(id), &kind);
  }
}

// The tables a ruleset document names by the role of a kind of content table.
void addRulesetTables(const VirtualFileSystem& vfs, const AssetId& ruleset,
                      std::vector<std::pair<AssetId, const TableKind*>>& tables) {
  const auto bytes = vfs.read(ruleset);
  if (!bytes) {
    return;
  }
  const auto document = core::parseYaml(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  const core::YamlNode* named =
      document && document->isMapping() ? document->find(RulesetTablesKey) : nullptr;
  if (named == nullptr || !named->isMapping()) {
    return;
  }
  for (const core::YamlEntry& entry : named->entries()) {
    const TableKind* kind = kindOfRole(entry.key);
    if (kind == nullptr || !entry.value.isScalar()) {
      continue;
    }
    if (auto id = AssetId::parse(entry.value.text()); id && vfs.resolve(*id) != nullptr) {
      addTable(tables, std::move(*id), *kind);
    }
  }
}

} // namespace

std::span<const TableKind> composedTableKinds() {
  return Kinds;
}

std::vector<std::pair<AssetId, const TableKind*>> composedTablesOf(const VirtualFileSystem& vfs) {
  std::vector<std::pair<AssetId, const TableKind*>> tables;
  for (const TableKind& kind : Kinds) {
    if (auto id = AssetId::parse(kind.defaultId); id && vfs.resolve(*id) != nullptr) {
      addTable(tables, std::move(*id), kind);
    }
  }
  // Any ruleset's manifest, rules/<id>/ruleset.
  const std::string rulesetPattern = std::format("{}/*/{}", RulesetsFolder, RulesetManifestLeaf);
  for (const AssetId& id : vfs.ids()) {
    if (matchesIdPattern(rulesetPattern, id)) {
      addRulesetTables(vfs, id, tables);
    }
  }
  return tables;
}

} // namespace dwe::assets
