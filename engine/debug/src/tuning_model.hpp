#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/debug/tuning.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/ruleset/ruleset_values.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::debug {

// The speeds the panel offers as buttons: stopped, the original's, and fast-forwards up to an hour per second.
inline constexpr std::array<double, 5> ClockSpeedPresets{0.0, 1.0, 10.0, 60.0, 3600.0};

// The texts of a flag's two values.
inline constexpr std::string_view FlagOnText = "true";
inline constexpr std::string_view FlagOffText = "false";

// How the panel edits a value of a section.
enum class TuningValueKind : std::uint8_t {
    Number, // a text field that takes numbers
    Flag,   // a checkbox: true or false, as the quirks
    Text,   // a text field: names, strategy names
    Group,  // a mapping or a sequence, shown as a tree
};

// A node of a section's tree in the panel.
struct TuningNode {
    // The key in its mapping, or the index in its sequence; empty for the root.
    std::string key;
    // The dotted path RulesetStore::setValue takes.
    std::string path;
    TuningValueKind kind = TuningValueKind::Group;
    // The text of a value.
    std::string text;
    // Whether the file quotes the value, so an edit keeps it a string.
    bool quoted = false;
    std::vector<TuningNode> children;
};

// A section of the active ruleset as the panel shows it.
struct TuningSection {
    std::string name;
    std::string owner;
    std::string file;
    TuningNode root;
};

// The tree of a section's document.
TuningNode buildTuningTree(const core::YamlNode& document);

// The YAML of a value typed for a node: a number must read as one, a flag must be true or false, and a text keeps the
// quoting of the file.
core::Result<std::string> yamlOfEdit(const TuningNode& node, std::string_view input);

// The game time as the panel shows it: "year 0, day 3, 08:15".
std::string describeTime(const ruleset::GameTime& time);

// The tuning panel without its widgets: the ruleset store and the packs seen as lists and trees, and the changes the
// panel asks for. Each change says whether it was accepted; one that is refused leaves its error in error() and the
// store as it was, and one that is accepted says what it did in status().
class TuningModel {
public:
    explicit TuningModel(const TuningTarget& target);

    [[nodiscard]] const TuningTarget& target() const { return target_; }

    // The ids of the rulesets in the packs, as listed at the start or by the last refreshRulesets().
    [[nodiscard]] const std::vector<std::string>& rulesets() const { return rulesets_; }
    void refreshRulesets();

    [[nodiscard]] const ruleset::RulesetInfo& active() const { return target_.store->current().info(); }
    [[nodiscard]] std::uint64_t revision() const { return target_.store->revision(); }

    // The sections of the active ruleset, rebuilt when the revision moves. The references stay valid until the next
    // call after a change.
    [[nodiscard]] const std::vector<TuningSection>& sections();

    // Switches to another ruleset.
    bool select(std::string_view id);

    // Sets a value of a section to what was typed for it.
    bool edit(std::string_view section, const TuningNode& node, std::string_view input);

    // Reads the active ruleset from its files again (without the checks of the systems) and keeps what reloading would
    // change: each tuned value it drops, and each file edit it takes. reloadChanges() has them.
    bool previewReload();
    [[nodiscard]] const std::vector<ruleset::ValueDifference>& reloadChanges() const { return reloadChanges_; }

    // Reloads the active ruleset from its files.
    bool reload();

    // Writes a section of the active ruleset back to its pack file, keeping its comments; status() names the file.
    bool save(std::string_view section);

    // The last refused change, or empty since the last accepted one.
    [[nodiscard]] const std::string& error() const { return error_; }
    // What the last accepted change did.
    [[nodiscard]] const std::string& status() const { return status_; }

private:
    // Records the outcome of a change: its status, or its error. Whether it was accepted.
    bool record(const core::Result<void>& outcome, std::string status);

    TuningTarget target_;
    std::vector<std::string> rulesets_;
    std::vector<TuningSection> sections_;
    std::uint64_t sectionsRevision_ = 0;
    std::vector<ruleset::ValueDifference> reloadChanges_;
    std::string error_;
    std::string status_;
};

} // namespace dwe::debug
