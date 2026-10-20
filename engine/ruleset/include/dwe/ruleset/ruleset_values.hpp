#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::ruleset {

// Joins the keys of a value path: "hunger" and "window_minutes" give "hunger.window_minutes", the paths
// Ruleset::setValue and findValue take.
std::string joinPath(std::string_view parent, std::string_view key);

// A value written as YAML on one line, the way emitYaml writes it inside a document: `90`, `"Original"`, `[]`. Fails
// on text that is not valid UTF-8, or on a value that does not fit on one line.
core::Result<std::string> inlineYaml(const core::YamlNode& value);

// A value of a ruleset: a scalar, or an empty sequence or mapping, at a path of a section.
struct RulesetValue {
    std::string section;
    std::string path;
    // The value as YAML, as emitYaml writes it without the final newline: `90`, `"Original"`, `[]`.
    std::string yaml;

    friend bool operator==(const RulesetValue&, const RulesetValue&) = default;
};

// The values of a section's document, in the order of the document.
std::vector<RulesetValue> listValues(std::string_view section, const core::YamlNode& document);

// Every value of a ruleset, by section name and then in the order of each document.
std::vector<RulesetValue> listValues(const Ruleset& ruleset);

// A value that two rulesets do not share: missing on one side, or different.
struct ValueDifference {
    std::string section;
    std::string path;
    std::optional<std::string> before;
    std::optional<std::string> after;

    friend bool operator==(const ValueDifference&, const ValueDifference&) = default;
};

// The values that differ between two rulesets, by section and path, sorted.
std::vector<ValueDifference> diffValues(const Ruleset& before, const Ruleset& after);

// The ids of the rulesets in the packs (every rules/<id>/ruleset), sorted.
std::vector<std::string> listRulesets(const assets::VirtualFileSystem& vfs);

} // namespace dwe::ruleset
