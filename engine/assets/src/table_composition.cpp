#include <dwe/assets/content_id.hpp>
#include <dwe/assets/messages.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/assets/table_composition.hpp>
#include <dwe/assets/virtual_file_system.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace dwe::assets {

namespace {

using core::YamlNode;

bool hasBaseRights(const TableContribution& contribution) {
    return assets::hasBaseRights(contribution.origin, contribution.layer);
}

std::string textOf(std::span<const std::byte> bytes) {
    return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::optional<std::int64_t> integerOf(const YamlNode& node) {
    if (!node.isScalar()) {
        return std::nullopt;
    }
    const std::string& text = node.text();
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

// A copy of an entry with its index right after its key.
YamlNode withIndex(const YamlNode& entry, std::string_view entryKey, std::int64_t index) {
    YamlNode numbered = YamlNode::mapping();
    for (const core::YamlEntry& field : entry.entries()) {
        numbered.set(field.key, field.value);
        if (field.key == entryKey) {
            numbered.set(std::string{EntryIndexKey}, YamlNode::scalar(std::to_string(index)));
        }
    }
    return numbered;
}

// The entries of one entry section, as the packs so far leave them.
class SectionComposer {
public:
    explicit SectionComposer(const EntrySection& rule) : rule_(&rule) {}

    // Adds a pack's entries of the section: they replace the entries with their ids and the rest go at the end.
    core::Result<void> add(const TableContribution& contribution, const YamlNode& entries) {
        std::set<std::string, std::less<>> listed;
        for (const YamlNode& entry : entries.items()) {
            auto id = idOf(contribution, entry);
            if (!id) {
                return std::unexpected{id.error()};
            }
            if (!listed.insert(id->str()).second) {
                return fail(messages::CompositionRepeatedKey, contribution, rule_->section, id->str());
            }
            const auto placed = positions_.find(id->str());
            auto added = placed == positions_.end() ? addEntry(contribution, *id, entry)
                                                    : replaceEntry(contribution, *id, entry, placed->second);
            if (!added) {
                return added;
            }
        }
        return {};
    }

    // Adds the ids of the entries whose index the engine gave, in a numbered section.
    void addEngineNumbered(std::set<std::string, std::less<>>& ids) const {
        if (!rule_->numbered) {
            return;
        }
        for (const auto& [id, position] : positions_) {
            if (!original_[position]) {
                ids.insert(id);
            }
        }
    }

    [[nodiscard]] YamlNode node() const {
        YamlNode sequence = YamlNode::sequence();
        for (const YamlNode& entry : entries_) {
            sequence.append(entry);
        }
        return sequence;
    }

private:
    template <typename... Args>
    static core::Result<void> fail(std::string_view message, const TableContribution& contribution, Args&&... args) {
        return core::fail(messages::formatMessage(message, contribution.file, std::forward<Args>(args)...));
    }

    [[nodiscard]] core::Result<ContentId> idOf(const TableContribution& contribution, const YamlNode& entry) const {
        const YamlNode* key = entry.isMapping() ? entry.find(rule_->entryKey) : nullptr;
        if (key == nullptr || !key->isScalar()) {
            return core::fail(messages::formatMessage(messages::CompositionNotEntry, contribution.file, rule_->section,
                                                      rule_->entryKey));
        }
        auto id = ContentId::parse(key->text());
        if (!id) {
            return core::fail(messages::formatMessage(messages::CompositionBadKey, contribution.file, key->text(),
                                                      rule_->section, id.error().message));
        }
        return id;
    }

    [[nodiscard]] core::Result<std::optional<std::int64_t>>
    givenIndex(const TableContribution& contribution, const ContentId& id, const YamlNode& entry) const {
        const YamlNode* given = entry.find(EntryIndexKey);
        if (given == nullptr) {
            return std::nullopt;
        }
        const std::optional<std::int64_t> index = integerOf(*given);
        if (!index) {
            return core::fail(messages::formatMessage(messages::CompositionBadIndex, contribution.file, id.str(),
                                                      rule_->section, given->isScalar() ? given->text() : ""));
        }
        return index;
    }

    [[nodiscard]] core::Result<void> checkNamespace(const TableContribution& contribution, const ContentId& id) const {
        if (!rule_->ownsIds) {
            return {};
        }
        if (!id.isNamespaced() && !hasBaseRights(contribution)) {
            return fail(messages::CompositionOriginalAdded, contribution, id.str(), rule_->section,
                        originName(contribution.origin), layerName(contribution.layer), contribution.packId);
        }
        if (id.isNamespaced() && id.packNamespace() != contribution.packId) {
            return fail(messages::CompositionForeignNamespace, contribution, id.str(), rule_->section,
                        contribution.packId);
        }
        return {};
    }

    core::Result<void> addEntry(const TableContribution& contribution, const ContentId& id, const YamlNode& entry) {
        if (auto checked = checkNamespace(contribution, id); !checked) {
            return checked;
        }
        auto given = givenIndex(contribution, id, entry);
        if (!given) {
            return std::unexpected{given.error()};
        }
        if (*given && !hasBaseRights(contribution)) {
            return fail(messages::CompositionIndexOfNewEntry, contribution, id.str(), rule_->section);
        }
        if (!rule_->numbered) {
            // The row's index, if the base gives one, is the place of the entry it is keyed by: kept, never made up.
            positions_.emplace(id.str(), entries_.size());
            entries_.push_back(entry);
            indices_.push_back(*given);
            original_.push_back(given->has_value());
            return {};
        }
        const std::int64_t index = given->value_or(nextIndex_);
        if (const auto owner = owners_.find(index); owner != owners_.end()) {
            return fail(messages::CompositionIndexTaken, contribution, id.str(), rule_->section, index, owner->second);
        }
        owners_.emplace(index, id.str());
        nextIndex_ = std::max(nextIndex_, index + 1);
        positions_.emplace(id.str(), entries_.size());
        entries_.push_back(*given ? entry : withIndex(entry, rule_->entryKey, index));
        indices_.emplace_back(index);
        original_.push_back(given->has_value());
        return {};
    }

    core::Result<void> replaceEntry(const TableContribution& contribution, const ContentId& id, const YamlNode& entry,
                                    std::size_t position) {
        auto given = givenIndex(contribution, id, entry);
        if (!given) {
            return std::unexpected{given.error()};
        }
        const std::optional<std::int64_t> kept = indices_[position];
        if (*given && !original_[position] && !hasBaseRights(contribution)) {
            // The engine numbers the entries the packs add: a higher pack cannot give one an index either, not even
            // the one the engine gave it, which depends on the packs mounted.
            return fail(messages::CompositionIndexOfNewEntry, contribution, id.str(), rule_->section);
        }
        if (*given && kept && **given != *kept) {
            return fail(messages::CompositionIndexChanged, contribution, id.str(), rule_->section, **given, *kept);
        }
        entries_[position] = (*given || !kept) ? entry : withIndex(entry, rule_->entryKey, *kept);
        return {};
    }

    const EntrySection* rule_;
    std::vector<YamlNode> entries_;
    std::vector<std::optional<std::int64_t>> indices_;
    std::vector<bool> original_; // whether the base gave the entry its index
    std::map<std::string, std::size_t, std::less<>> positions_;
    std::map<std::int64_t, std::string> owners_; // the id that has each index of a numbered section
    std::int64_t nextIndex_ = 0;
};

// The table as the packs so far leave it: whole sections, and the entry sections being composed.
class TableComposer {
public:
    explicit TableComposer(const TableComposition& composition) : composition_(&composition) {}

    core::Result<void> add(const TableContribution& contribution) {
        if (!contribution.document.isMapping()) {
            return core::fail(messages::formatMessage(messages::CompositionNotMapping, contribution.file));
        }
        for (const core::YamlEntry& section : contribution.document.entries()) {
            const EntrySection* rule = ruleOf(section.key);
            if (rule != nullptr && !section.value.isSequence()) {
                // A list of entries never empties by a null or a scalar: a pack cannot remove entries.
                return core::fail(
                    messages::formatMessage(messages::CompositionNotList, contribution.file, section.key));
            }
            if (rule == nullptr) {
                composers_.erase(section.key);
                table_.set(section.key, section.value);
                continue;
            }
            auto [composer, added] = composers_.try_emplace(section.key, *rule);
            if (added) {
                table_.set(section.key, YamlNode::sequence());
            }
            if (auto composed = composer->second.add(contribution, section.value); !composed) {
                return composed;
            }
        }
        return {};
    }

    [[nodiscard]] std::set<std::string, std::less<>> engineNumbered() const {
        std::set<std::string, std::less<>> ids;
        for (const auto& [section, composer] : composers_) {
            composer.addEngineNumbered(ids);
        }
        return ids;
    }

    [[nodiscard]] YamlNode result() const {
        YamlNode table = table_;
        for (const auto& [section, composer] : composers_) {
            table.set(section, composer.node());
        }
        return table;
    }

private:
    [[nodiscard]] const EntrySection* ruleOf(std::string_view section) const {
        const auto found = std::ranges::find(composition_->sections, section, &EntrySection::section);
        return found == composition_->sections.end() ? nullptr : &*found;
    }

    const TableComposition* composition_;
    YamlNode table_ = YamlNode::mapping();
    std::map<std::string, SectionComposer, std::less<>> composers_;
};

const MountedPack* packOf(const VirtualFileSystem& vfs, std::string_view packId) {
    const auto found = std::ranges::find(vfs.packs(), packId,
                                         [](const MountedPack& pack) -> std::string_view { return pack.manifest.id; });
    return found == vfs.packs().end() ? nullptr : &*found;
}

core::Result<TableContribution> readContribution(const VirtualFileSystem& vfs, const AssetSource& source) {
    const MountedPack* pack = packOf(vfs, source.packId);
    if (pack == nullptr) {
        return core::fail(messages::formatMessage(messages::NoPackOfFile, pathText(source.file), source.packId));
    }
    auto bytes = pack->fileSystem->readFile(source.file);
    if (!bytes) {
        return core::fail(messages::formatMessage(messages::InFile, pathText(source.file), bytes.error().message));
    }
    auto document = core::parseYaml(textOf(*bytes));
    if (!document) {
        return core::fail(messages::formatMessage(messages::InFile, pathText(source.file), document.error().message));
    }
    return TableContribution{.packId = source.packId,
                             .layer = pack->manifest.layer,
                             .origin = pack->origin,
                             .file = pathText(source.file),
                             .document = std::move(*document)};
}

} // namespace

core::Result<core::YamlNode> composeTable(std::span<const TableContribution> contributions,
                                          const TableComposition& composition) {
    TableComposer composer{composition};
    for (const TableContribution& contribution : contributions) {
        if (auto added = composer.add(contribution); !added) {
            return std::unexpected{added.error()};
        }
    }
    return composer.result();
}

core::Result<ComposedTableText> readComposedTableText(const VirtualFileSystem& vfs, const AssetId& id,
                                                      const TableComposition& composition) {
    const std::span<const AssetSource> sources = vfs.sources(id);
    if (sources.empty()) {
        auto missing = vfs.read(id);
        return std::unexpected{missing.error()};
    }
    std::vector<TableContribution> contributions;
    for (const AssetSource& source : sources) {
        auto contribution = readContribution(vfs, source);
        if (!contribution) {
            return std::unexpected{contribution.error()};
        }
        contributions.push_back(std::move(*contribution));
    }
    TableComposer composer{composition};
    for (const TableContribution& contribution : contributions) {
        if (auto added = composer.add(contribution); !added) {
            return std::unexpected{added.error()};
        }
    }
    const YamlNode table = composer.result();
    ComposedTableText composed{.text = {}, .engineNumbered = composer.engineNumbered()};
    if (contributions.size() == 1 && table == contributions.front().document) {
        // Checked and unchanged: the file's own text, so the readers' errors point at its lines.
        auto bytes = vfs.read(id);
        if (!bytes) {
            return std::unexpected{bytes.error()};
        }
        composed.text = textOf(*bytes);
        return composed;
    }
    auto text = core::emitYaml(table);
    if (!text) {
        return std::unexpected{text.error()};
    }
    composed.text = std::move(*text);
    return composed;
}

core::Result<std::string> readComposedTable(const VirtualFileSystem& vfs, const AssetId& id,
                                            const TableComposition& composition) {
    auto composed = readComposedTableText(vfs, id, composition);
    if (!composed) {
        return std::unexpected{composed.error()};
    }
    return std::move(composed->text);
}

} // namespace dwe::assets
