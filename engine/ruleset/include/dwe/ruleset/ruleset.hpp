#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/table_kinds.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>

#include <functional>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::ruleset {

// Every ruleset lives in a pack under rules/<id>/: the manifest rules/<id>/ruleset.yaml and one rules/<id>/<name>.yaml
// per section (docs/architecture/rulesets.md).
inline constexpr std::string_view RulesFolder = assets::RulesetsFolder;
inline constexpr std::string_view ManifestLeaf = assets::RulesetManifestLeaf;

// The ruleset of the original game's rules (ADR 0022), the one the game starts with.
inline constexpr std::string_view OriginalRuleset = "original";

// How many rulesets an `extends` chain may have, so a long chain is reported instead of followed.
inline constexpr std::size_t MaxExtendsDepth = 8;

// A section of a ruleset: the YAML of rules/<owner>/<name>.yaml, which its system parses into its own struct.
struct Section {
    std::string name;
    // The ruleset whose file it is: the one loaded, or one it extends that this one does not override.
    std::string owner;
    // Where it came from, for messages: the file of the pack that provides it.
    std::string file;
    core::YamlNode node;

    // An error at a node of the section: "<file>: line L, column C: <problem>". Nodes set in code have no position.
    [[nodiscard]] core::Error error(const core::YamlNode& at, std::string_view problem) const;
};

// Checks a section the way its system reads it, normally by parsing it and dropping the result. Each system gives one
// for each section it owns, so a load, a reload or a tuned value is refused before any system sees it.
using SectionCheck = std::function<core::Result<void>(const Section&)>;

// The checks by section name. A section without a check is handed out as it is (dwx validate still checks it against
// its schema).
using SectionChecks = std::map<std::string, SectionCheck, std::less<>>;

// What the manifest says about the ruleset.
struct RulesetInfo {
    std::string id;
    std::string name;
    std::string version;
    std::string description;
    // The id and then each ruleset it extends, nearest first: {"hard", "original"}.
    std::vector<std::string> chain;
};

// A loaded ruleset, with its `extends` chain resolved: the sections and tables of the rulesets it extends that it
// does not override, and its own. It only hands out YAML and asset ids; it does not know what the sections mean.
class Ruleset {
public:
    [[nodiscard]] const RulesetInfo& info() const { return info_; }
    [[nodiscard]] const std::string& id() const { return info_.id; }

    // A section by name, or nullptr if the ruleset has none.
    [[nodiscard]] const Section* section(std::string_view name) const;

    // A section that must be there: an error that names the ruleset if it is not.
    [[nodiscard]] core::Result<const Section*> requireSection(std::string_view name) const;

    // Every section, by name.
    [[nodiscard]] const std::map<std::string, Section, std::less<>>& sections() const { return sections_; }

    // The asset id of a table by its role (digimon, raise, evolution...), or nullptr if the ruleset names none.
    [[nodiscard]] const assets::AssetId* table(std::string_view role) const;

    // Every table, by role.
    [[nodiscard]] const std::map<std::string, assets::AssetId, std::less<>>& tables() const { return tables_; }

    // The Lua files of the ruleset and of those it extends, for strategies written in Lua.
    [[nodiscard]] const std::vector<assets::AssetId>& scripts() const { return scripts_; }

    // Sets the value at a path of a section ("hunger.window_minutes", "patterns.2.hours") to a YAML text. The path
    // must exist. Nothing is checked: RulesetStore::setValue checks the result before it replaces the ruleset.
    core::Result<void> setValue(std::string_view section, std::string_view path, std::string_view yaml);

private:
    friend core::Result<Ruleset> loadRuleset(const assets::VirtualFileSystem& vfs, std::string_view id,
                                             const SectionChecks& checks);

    RulesetInfo info_;
    std::map<std::string, Section, std::less<>> sections_;
    std::map<std::string, assets::AssetId, std::less<>> tables_;
    std::vector<assets::AssetId> scripts_;
};

// Loads rules/<id>/ruleset.yaml and its sections from the packs, follows `extends`, checks that every table and script
// it names is in the packs, and runs the clock check and `checks` on the sections. The errors name the file, line and
// column.
core::Result<Ruleset> loadRuleset(const assets::VirtualFileSystem& vfs, std::string_view id,
                                  const SectionChecks& checks = {});

// Runs the clock check and `checks` on every section they cover; the first error.
core::Result<void> checkRuleset(const Ruleset& ruleset, const SectionChecks& checks);

// The key of a section's quirks: the original's oddities and bugs, each a named flag. true reproduces the original
// (ADR 0022: the `original` ruleset keeps them all); a later ruleset sets a flag to false to fix it.
inline constexpr std::string_view QuirksKey = "quirks";

// The quirk flags of a section, read with readQuirks.
class Quirks {
public:
    // Whether the quirk is on; false for a name readQuirks was not given.
    [[nodiscard]] bool on(std::string_view name) const {
        const auto found = flags_.find(name);
        return found != flags_.end() && found->second;
    }

private:
    friend Quirks readQuirks(assets::YamlReader& reader, const core::YamlNode& node,
                             std::initializer_list<std::string_view> names);

    std::map<std::string, bool, std::less<>> flags_;
};

// Reads the `quirks` mapping of a section with the reader a system parses its section with: exactly these names, each
// true or false. A missing or unknown flag is an error, so every ruleset decides each quirk.
Quirks readQuirks(assets::YamlReader& reader, const core::YamlNode& node,
                  std::initializer_list<std::string_view> names);

// The node at a dotted path of a document ("hunger.window_minutes"; a number picks an item of a sequence), or nullptr.
const core::YamlNode* findValue(const core::YamlNode& root, std::string_view path);

// The asset id of a ruleset's manifest: rules/<id>/ruleset.
core::Result<assets::AssetId> manifestId(std::string_view id);

} // namespace dwe::ruleset
