#pragma once

#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A class of entries of a table that a ruleset names, such as the meals among the items or the finishers among the
// moves (docs/architecture/rulesets.md#rangos-e-ids). Ranges and thresholds count the original's numbers, so they
// cover only the entries whose number the base pack gave (originalIndex): never one a pack added, whatever number the
// engine gave it. A pack's new entry joins a class by its id.
namespace dwe::assets {

// Entries from `from` to `to`, both included, by their original number.
struct IndexRange {
    std::int32_t from = 0;
    std::int32_t to = 0;

    friend bool operator==(const IndexRange&, const IndexRange&) = default;
};

struct EntryClass {
    // NOLINTNEXTLINE(readability-redundant-member-init): the default lets designated initializers leave it out.
    std::vector<IndexRange> ranges = {};
    // The entries with a lower original number.
    std::optional<std::int32_t> below = std::nullopt;
    // The entries named by id: those a pack adds, or any other.
    // NOLINTNEXTLINE(readability-redundant-member-init): the default lets designated initializers leave it out.
    std::vector<std::string> ids = {};

    // Whether the class holds the entry (an ItemEntry or a MoveEntry).
    template <typename Entry>
    [[nodiscard]] bool contains(const Entry& entry) const {
        if (std::ranges::contains(ids, entry.id)) {
            return true;
        }
        if (!entry.originalIndex) {
            return false;
        }
        if (below && entry.index < *below) {
            return true;
        }
        return std::ranges::any_of(
            ranges, [&](const IndexRange& range) { return entry.index >= range.from && entry.index <= range.to; });
    }

    friend bool operator==(const EntryClass&, const EntryClass&) = default;
};

// Reads a list whose items are ranges `{from: <number>, to: <number>}` of original numbers or ids of entries, into
// `into`. Errors are the reader's.
void readEntryList(YamlReader& reader, const core::YamlNode& node, std::string_view what, EntryClass& into);

// Reads a list of ids of entries into `into`.
void readEntryIds(YamlReader& reader, const core::YamlNode& node, std::string_view what, EntryClass& into);

// Checks a class against the session's table (an ItemTable or a MoveTable): every number of a range is an entry of
// the original, and every id is an entry. `rule` names the class in the error, `what` the table.
template <typename Table>
core::Result<void> checkEntryClass(const EntryClass& entries, const Table& table, std::string_view rule,
                                   std::string_view what);

// The error of checkEntryClass for a number the original does not have.
core::Error entryClassNumberError(std::string_view rule, std::int32_t number, std::string_view what);

// The error of checkEntryClass for an id the table does not have.
core::Error entryClassIdError(std::string_view rule, std::string_view id, std::string_view what);

template <typename Table>
core::Result<void> checkEntryClass(const EntryClass& entries, const Table& table, std::string_view rule,
                                   std::string_view what) {
    for (const IndexRange& range : entries.ranges) {
        for (std::int32_t number = range.from; number <= range.to; ++number) {
            if (table.findByOriginalIndex(number) == nullptr) {
                return std::unexpected{entryClassNumberError(rule, number, what)};
            }
        }
    }
    for (const std::string& id : entries.ids) {
        if (table.find(id) == nullptr) {
            return std::unexpected{entryClassIdError(rule, id, what)};
        }
    }
    return {};
}

} // namespace dwe::assets
