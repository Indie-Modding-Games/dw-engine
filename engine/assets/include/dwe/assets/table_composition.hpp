#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>

#include <span>
#include <string>
#include <string_view>

// How the packs' files of one content table combine into the table the game reads, entry by entry
// (docs/architecture/vfs.md#tablas-por-entradas), so a mod adds or replaces one Digimon without copying the table.
namespace dwe::assets {

class VirtualFileSystem;

// The key of an entry's original number: internal to the engine, which gives one to each new entry.
inline constexpr std::string_view EntryIndexKey = "index";

// A section of a table that is a list of entries, each named by the content id in `entryKey` (ContentId).
struct EntrySection {
    std::string_view section;
    std::string_view entryKey;
    // Whether `index` is the entry's own number, unique in the section, which the engine gives to a new entry. In a
    // section keyed by another table's id, a base row's `index` is the place of the entry it belongs to: it is kept
    // when a pack replaces the row, and a new row has none.
    bool numbered = true;
    // Whether the key is the entry's own id, so a pack that adds an entry names it in its namespace; a section keyed
    // by another table's id (a raise row of a Digimon) adds rows for any id.
    bool ownsIds = true;
};

// The sections of a table that compose entry by entry. Any other section of a higher pack replaces the whole
// section below it, as the ruleset's `extends` does.
struct TableComposition {
    std::span<const EntrySection> sections;
};

// A pack's file of a table, as composeTable takes it.
struct TableContribution {
    std::string packId;
    Layer layer = Layer::Base;
    // Only a base pack of the engine's own (PackOrigin::Engine) has the base's rights: adding ids without a namespace
    // and giving indices. A mod that declares the base layer has none of them.
    PackOrigin origin = PackOrigin::Mod;
    std::string file; // for the errors
    core::YamlNode document;
};

// Composes the files of a table from the bottom of the stack up. In an entry section, an entry whose id is already
// there replaces that entry in its place and keeps its index; a new one goes at the end, in stack order and then in
// file order, with the next free index. Any pack but the engine's own base packs names its new entries
// `<its id>:<name>`.
core::Result<core::YamlNode> composeTable(std::span<const TableContribution> contributions,
                                          const TableComposition& composition);

// The YAML text of a table of the packs, composed by composeTable from every pack that has it. With a single pack the
// text is that pack's file as it is.
core::Result<std::string> readComposedTable(const VirtualFileSystem& vfs, const AssetId& id,
                                            const TableComposition& composition);

} // namespace dwe::assets
