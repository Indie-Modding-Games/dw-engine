#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/ruleset/game_clock.hpp>
#include <dwe/ruleset/messages.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace dwe::ruleset {

namespace {

using assets::AssetId;
using assets::YamlReader;
using core::YamlNode;
using messages::formatMessage;

// The keys of a manifest.
constexpr std::string_view IdKey = "id";
constexpr std::string_view NameKey = "name";
constexpr std::string_view VersionKey = "version";
constexpr std::string_view DescriptionKey = "description";
constexpr std::string_view ExtendsKey = "extends";
constexpr std::string_view TablesKey = "tables";
constexpr std::string_view SectionsKey = "sections";
constexpr std::string_view ScriptsKey = "scripts";

constexpr std::string_view ScriptExtension = ".lua";
constexpr char PathSeparator = '.';
constexpr char FolderSeparator = '/';

std::string_view textOf(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// A ruleset id, a section name or a table role: one segment of an asset id.
bool isName(std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

// The path of a file of a ruleset's folder, from the pack's root: rules/<id>/<leaf>.
std::string rulesetPath(std::string_view id, std::string_view leaf) {
    std::string path{RulesFolder};
    path += FolderSeparator;
    path += id;
    path += FolderSeparator;
    path += leaf;
    return path;
}

// A YAML document of the packs, parsed, and the file it came from.
struct ParsedFile {
    std::string file;
    YamlNode node;
};

core::Result<ParsedFile> readYamlAsset(const assets::VirtualFileSystem& vfs, const AssetId& id, std::string_view what) {
    const assets::AssetSource* source = vfs.resolve(id);
    if (source == nullptr) {
        return core::fail(formatMessage(messages::NotInPacks, what, id.str()));
    }
    std::string file = assets::pathText(source->file);
    auto bytes = vfs.read(id);
    if (!bytes) {
        return core::fail(formatMessage(messages::InFile, file, bytes.error().message));
    }
    auto node = core::parseYaml(textOf(*bytes));
    if (!node) {
        return core::fail(formatMessage(messages::InFile, file, node.error().message));
    }
    return ParsedFile{.file = std::move(file), .node = std::move(*node)};
}

// What one manifest says, before its `extends` is followed.
struct Manifest {
    RulesetInfo info;
    std::optional<std::string> extends;
    std::map<std::string, AssetId, std::less<>> tables;
    std::vector<std::string> sections;
    std::vector<AssetId> scripts;
};

void readInfo(YamlReader& reader, const YamlNode& root, std::string_view id, RulesetInfo& info) {
    const YamlNode& idNode = reader.field(root, IdKey);
    info.id = reader.text(idNode);
    if (reader.ok() && info.id != id) {
        reader.fail(idNode, formatMessage(messages::WrongId, id));
    }
    info.name = reader.text(reader.field(root, NameKey));
    const YamlNode& version = reader.field(root, VersionKey);
    info.version = reader.text(version);
    if (reader.ok()) {
        if (auto parsed = assets::parseVersion(info.version); !parsed) {
            reader.fail(version, parsed.error().message);
        }
    }
    if (const YamlNode* description = root.find(DescriptionKey)) {
        info.description = reader.text(*description);
    }
}

std::optional<std::string> readExtends(YamlReader& reader, const YamlNode& root) {
    const YamlNode* extends = root.find(ExtendsKey);
    if (extends == nullptr) {
        return std::nullopt;
    }
    std::string base = reader.text(*extends);
    if (reader.ok() && !isName(base)) {
        reader.fail(*extends, messages::BadExtends);
    }
    return base;
}

void readTable(YamlReader& reader, const core::YamlEntry& entry, const assets::VirtualFileSystem& vfs,
               Manifest& manifest) {
    if (!isName(entry.key)) {
        reader.fail(entry.value, formatMessage(messages::BadName, messages::TableRoleText, entry.key));
        return;
    }
    auto id = AssetId::parse(reader.text(entry.value));
    if (!reader.ok()) {
        return;
    }
    if (!id) {
        reader.fail(entry.value, formatMessage(messages::BadTable, entry.key, id.error().message));
    } else if (vfs.resolve(*id) == nullptr) {
        reader.fail(entry.value, formatMessage(messages::MissingTable, entry.key, id->str()));
    } else {
        manifest.tables.insert_or_assign(entry.key, std::move(*id));
    }
}

void readTables(YamlReader& reader, const YamlNode& root, const assets::VirtualFileSystem& vfs, Manifest& manifest) {
    const YamlNode* tables = root.find(TablesKey);
    if (tables == nullptr || !reader.ok()) {
        return;
    }
    if (!tables->isMapping()) {
        reader.fail(*tables, messages::TablesNotMapping);
        return;
    }
    for (const core::YamlEntry& entry : tables->entries()) {
        readTable(reader, entry, vfs, manifest);
    }
}

void readSections(YamlReader& reader, const YamlNode& root, Manifest& manifest) {
    for (const YamlNode& item : reader.sequence(reader.field(root, SectionsKey), SectionsKey)) {
        std::string name = reader.text(item);
        if (!reader.ok()) {
            return;
        }
        if (!isName(name) || name == ManifestLeaf) {
            reader.fail(item, formatMessage(messages::BadSection, name, ManifestLeaf));
        } else if (std::ranges::contains(manifest.sections, name)) {
            reader.fail(item, formatMessage(messages::RepeatedSection, name));
        } else {
            manifest.sections.push_back(std::move(name));
        }
    }
}

void readScript(YamlReader& reader, const YamlNode& item, const assets::VirtualFileSystem& vfs, Manifest& manifest) {
    const std::string file = reader.text(item);
    if (!reader.ok()) {
        return;
    }
    auto id = assets::assetIdOfFile(rulesetPath(manifest.info.id, file));
    if (!file.ends_with(ScriptExtension) || file.contains(FolderSeparator) || !id) {
        reader.fail(item, formatMessage(messages::BadScript, file));
        return;
    }
    const assets::AssetSource* source = vfs.resolve(*id);
    if (source == nullptr || source->file.extension().string() != ScriptExtension) {
        reader.fail(item, formatMessage(messages::MissingScript, file));
        return;
    }
    manifest.scripts.push_back(std::move(*id));
}

void readScripts(YamlReader& reader, const YamlNode& root, const assets::VirtualFileSystem& vfs, Manifest& manifest) {
    const YamlNode* scripts = root.find(ScriptsKey);
    if (scripts == nullptr || !reader.ok()) {
        return;
    }
    for (const YamlNode& item : reader.sequence(*scripts, ScriptsKey)) {
        readScript(reader, item, vfs, manifest);
    }
}

core::Result<Manifest> readManifest(const assets::VirtualFileSystem& vfs, std::string_view id) {
    auto asset = manifestId(id);
    if (!asset) {
        return std::unexpected{asset.error()};
    }
    auto parsed = readYamlAsset(vfs, *asset, messages::RulesetText);
    if (!parsed) {
        return std::unexpected{parsed.error()};
    }
    const YamlNode& root = parsed->node;
    YamlReader reader;
    if (!reader.expectKeys(root, messages::ManifestText, {IdKey, NameKey, VersionKey, SectionsKey},
                           {DescriptionKey, ExtendsKey, TablesKey, ScriptsKey})) {
        return std::unexpected{reader.error(parsed->file)};
    }
    Manifest manifest;
    readInfo(reader, root, id, manifest.info);
    manifest.extends = readExtends(reader, root);
    readTables(reader, root, vfs, manifest);
    readSections(reader, root, manifest);
    readScripts(reader, root, vfs, manifest);
    return reader.finish(std::move(manifest), parsed->file);
}

std::vector<std::string_view> keysOf(std::string_view path) {
    std::vector<std::string_view> keys;
    for (const auto part : std::views::split(path, PathSeparator)) {
        keys.emplace_back(part);
    }
    return keys;
}

std::optional<std::size_t> indexOf(std::string_view key) {
    std::size_t index = 0;
    const auto [end, error] = std::from_chars(key.data(), key.data() + key.size(), index);
    if (error != std::errc{} || end != key.data() + key.size()) {
        return std::nullopt;
    }
    return index;
}

const YamlNode* childOf(const YamlNode& node, std::string_view key) {
    if (node.isMapping()) {
        return node.find(key);
    }
    if (node.isSequence()) {
        const auto index = indexOf(key);
        return index && *index < node.items().size() ? &node.items()[*index] : nullptr;
    }
    return nullptr;
}

YamlNode withItem(const YamlNode& sequence, std::size_t index, const YamlNode& item) {
    YamlNode copy = YamlNode::sequence();
    for (std::size_t at = 0; at < sequence.items().size(); ++at) {
        copy.append(at == index ? item : sequence.items()[at]);
    }
    return copy;
}

// A copy of `node` with the value at `keys` replaced.
core::Result<YamlNode> withValue(const YamlNode& node, std::span<const std::string_view> keys, YamlNode value) {
    if (keys.empty()) {
        return value;
    }
    const YamlNode* next = childOf(node, keys.front());
    if (next == nullptr) {
        return core::fail(formatMessage(messages::NoKey, keys.front()));
    }
    auto inner = withValue(*next, keys.subspan(1), std::move(value));
    if (!inner) {
        return std::unexpected{inner.error()};
    }
    if (node.isSequence()) {
        return withItem(node, indexOf(keys.front()).value_or(0), *inner);
    }
    YamlNode copy = node;
    copy.set(std::string{keys.front()}, std::move(*inner));
    return copy;
}

// A ruleset as loadChain builds it, before loadRuleset makes it a Ruleset.
struct RulesetParts {
    RulesetInfo info;
    std::map<std::string, Section, std::less<>> sections;
    std::map<std::string, AssetId, std::less<>> tables;
    std::vector<AssetId> scripts;
};

core::Result<void> addSections(const assets::VirtualFileSystem& vfs, const Manifest& manifest, RulesetParts& parts) {
    for (const std::string& name : manifest.sections) {
        auto asset = AssetId::parse(rulesetPath(manifest.info.id, name));
        if (!asset) {
            return std::unexpected{asset.error()};
        }
        auto parsed = readYamlAsset(vfs, *asset, messages::SectionText);
        if (!parsed) {
            return std::unexpected{parsed.error()};
        }
        parts.sections.insert_or_assign(name, Section{.name = name,
                                                      .owner = manifest.info.id,
                                                      .file = std::move(parsed->file),
                                                      .node = std::move(parsed->node)});
    }
    return {};
}

// Puts this manifest's parts over those of the ruleset it extends.
core::Result<void> addManifest(const assets::VirtualFileSystem& vfs, Manifest manifest, RulesetParts& parts) {
    for (auto& [role, table] : manifest.tables) {
        parts.tables.insert_or_assign(role, std::move(table));
    }
    if (auto added = addSections(vfs, manifest, parts); !added) {
        return added;
    }
    parts.scripts.insert(parts.scripts.end(), manifest.scripts.begin(), manifest.scripts.end());
    std::vector<std::string> extended = std::move(parts.info.chain);
    parts.info = std::move(manifest.info);
    parts.info.chain.push_back(parts.info.id);
    parts.info.chain.insert(parts.info.chain.end(), extended.begin(), extended.end());
    return {};
}

core::Result<void> checkChain(std::string_view id, const std::vector<std::string>& chain) {
    if (std::ranges::contains(chain, id)) {
        return core::fail(formatMessage(messages::ExtendsCycle, id, chain.back()));
    }
    if (chain.size() >= MaxExtendsDepth) {
        return core::fail(formatMessage(messages::ExtendsTooDeep, chain.front(), MaxExtendsDepth));
    }
    return {};
}

core::Result<RulesetParts> loadChain(const assets::VirtualFileSystem& vfs, std::string_view id,
                                     std::vector<std::string>& chain) {
    if (auto checked = checkChain(id, chain); !checked) {
        return std::unexpected{checked.error()};
    }
    chain.emplace_back(id);
    auto manifest = readManifest(vfs, id);
    if (!manifest) {
        return std::unexpected{manifest.error()};
    }
    RulesetParts parts;
    if (manifest->extends) {
        auto base = loadChain(vfs, *manifest->extends, chain);
        if (!base) {
            return std::unexpected{base.error()};
        }
        parts = std::move(*base);
    }
    if (auto added = addManifest(vfs, std::move(*manifest), parts); !added) {
        return std::unexpected{added.error()};
    }
    return parts;
}

} // namespace

core::Error Section::error(const YamlNode& at, std::string_view problem) const {
    return core::Error{formatMessage(messages::AtNode, file, at.line(), at.column(), problem)};
}

const Section* Ruleset::section(std::string_view name) const {
    const auto found = sections_.find(name);
    return found == sections_.end() ? nullptr : &found->second;
}

core::Result<const Section*> Ruleset::requireSection(std::string_view name) const {
    if (const Section* found = section(name)) {
        return found;
    }
    return core::fail(formatMessage(messages::NoSection, info_.id, name));
}

const AssetId* Ruleset::table(std::string_view role) const {
    const auto found = tables_.find(role);
    return found == tables_.end() ? nullptr : &found->second;
}

core::Result<void> Ruleset::setValue(std::string_view section, std::string_view path, std::string_view yaml) {
    const auto found = sections_.find(section);
    if (found == sections_.end()) {
        return core::fail(formatMessage(messages::NoSection, info_.id, section));
    }
    auto value = core::parseYaml(yaml);
    if (!value) {
        return core::fail(formatMessage(messages::AtPath, section, path, value.error().message));
    }
    auto node = withValue(found->second.node, keysOf(path), std::move(*value));
    if (!node) {
        return core::fail(formatMessage(messages::AtPath, section, path, node.error().message));
    }
    found->second.node = std::move(*node);
    return {};
}

core::Result<AssetId> manifestId(std::string_view id) {
    if (!isName(id)) {
        return core::fail(formatMessage(messages::BadName, messages::RulesetIdText, id));
    }
    return AssetId::parse(rulesetPath(id, ManifestLeaf));
}

const YamlNode* findValue(const YamlNode& root, std::string_view path) {
    const YamlNode* node = &root;
    for (const std::string_view key : keysOf(path)) {
        node = childOf(*node, key);
        if (node == nullptr) {
            return nullptr;
        }
    }
    return node;
}

Quirks readQuirks(YamlReader& reader, const YamlNode& node, std::initializer_list<std::string_view> names) {
    Quirks quirks;
    if (!reader.expectKeys(node, messages::QuirksText, names)) {
        return quirks;
    }
    for (const std::string_view name : names) {
        quirks.flags_.insert_or_assign(std::string{name}, reader.boolean(reader.field(node, name)));
    }
    return quirks;
}

core::Result<Ruleset> loadRuleset(const assets::VirtualFileSystem& vfs, std::string_view id,
                                  const SectionChecks& checks) {
    std::vector<std::string> chain;
    auto parts = loadChain(vfs, id, chain);
    if (!parts) {
        return std::unexpected{parts.error()};
    }
    Ruleset ruleset;
    ruleset.info_ = std::move(parts->info);
    ruleset.sections_ = std::move(parts->sections);
    ruleset.tables_ = std::move(parts->tables);
    ruleset.scripts_ = std::move(parts->scripts);
    if (auto checked = checkRuleset(ruleset, checks); !checked) {
        return std::unexpected{checked.error()};
    }
    return ruleset;
}

core::Result<void> checkRuleset(const Ruleset& ruleset, const SectionChecks& checks) {
    if (const Section* clock = ruleset.section(ClockSection)) {
        if (auto read = readClockRules(*clock); !read) {
            return std::unexpected{read.error()};
        }
    }
    for (const auto& [name, check] : checks) {
        const Section* section = ruleset.section(name);
        if (section == nullptr) {
            continue;
        }
        if (auto checked = check(*section); !checked) {
            return checked;
        }
    }
    return {};
}

} // namespace dwe::ruleset
