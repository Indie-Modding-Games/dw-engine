#include <dwe/assets/new_game_story.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <format>
#include <string_view>
#include <utility>
#include <vector>

#include "document_text.hpp"

namespace dwe::assets {

namespace {

using core::YamlNode;

constexpr std::string_view NewGameStoryId = "data/new_game_story";
constexpr std::string_view Context = "new game story";
// script_interp.c: initializeScripts clears this many trigger bits.
constexpr std::int32_t TriggerCount = 800;

// A sorted list of trigger numbers.
std::vector<std::int32_t> triggers(YamlReader& reader, const YamlNode& list, std::string_view what) {
    std::vector<std::int32_t> numbers;
    for (const YamlNode& trigger : reader.sequence(list, what)) {
        const auto value = reader.integer<std::int32_t>(trigger);
        if (reader.ok() && (value < 0 || value >= TriggerCount)) {
            reader.fail(trigger, std::format("a trigger is a number from 0 to {}", TriggerCount - 1));
        }
        numbers.push_back(value);
    }
    std::ranges::sort(numbers);
    return numbers;
}

// The `opening` block: where the spawn's step cell goes and what it sets.
NewGameOpening opening(YamlReader& reader, const YamlNode& node) {
    NewGameOpening result;
    if (!reader.expectKeys(node, "opening", {"exit", "section", "to", "sets"})) {
        return result;
    }
    result.exit = reader.integer<std::int32_t>(reader.field(node, "exit"));
    result.section = reader.integer<std::int32_t>(reader.field(node, "section"));
    const YamlNode& to = reader.field(node, "to");
    if (reader.expectKeys(to, "the opening's destination", {"exit"}, {"map", "screen"})) {
        if (const YamlNode* map = to.find("map"); map != nullptr) {
            result.toMap = reader.text(*map);
        }
        if (const YamlNode* screen = to.find("screen"); screen != nullptr) {
            result.toScreen = reader.integer<std::int32_t>(*screen);
        }
        if (result.toMap.has_value() == result.toScreen.has_value()) {
            reader.fail(to, "the opening goes to a map or to a screen");
        }
        result.toExit = reader.integer<std::int32_t>(reader.field(to, "exit"));
    }
    result.sets = triggers(reader, reader.field(node, "sets"), "sets");
    return result;
}

} // namespace

core::Result<NewGameStory> parseNewGameStory(std::string_view yaml) {
    auto tree = core::parseYaml(yaml);
    if (!tree) {
        return std::unexpected{core::Error{std::format("{}: {}", Context, tree.error().message)}};
    }
    YamlReader reader;
    const YamlNode& root = *tree;
    if (!reader.expectKeys(root, "the new game story", {"map", "script", "triggers_set"},
                           {"agreed", "triggers_unknown", "opening"})) {
        return std::unexpected{reader.error(Context)};
    }
    NewGameStory story{.map = reader.text(reader.field(root, "map")),
                       .script = reader.integer<std::int32_t>(reader.field(root, "script")),
                       .triggersSet = {},
                       .agreed = true,
                       .triggersUnknown = {},
                       .opening = {}};
    if (const YamlNode* agreed = root.find("agreed"); agreed != nullptr) {
        story.agreed = reader.boolean(*agreed);
    }
    story.triggersSet = triggers(reader, reader.field(root, "triggers_set"), "triggers_set");
    if (const YamlNode* unknown = root.find("triggers_unknown"); unknown != nullptr) {
        story.triggersUnknown = triggers(reader, *unknown, "triggers_unknown");
    }
    if (const YamlNode* found = root.find("opening"); found != nullptr) {
        story.opening = opening(reader, *found);
    }
    return reader.finish(std::move(story), Context);
}

core::Result<std::optional<NewGameStory>> loadNewGameStory(const VirtualFileSystem& vfs) {
    auto id = AssetId::parse(NewGameStoryId);
    if (!id) {
        return std::unexpected{id.error()};
    }
    if (vfs.resolve(*id) == nullptr) {
        return std::optional<NewGameStory>{};
    }
    auto text = readDocumentText(vfs, NewGameStoryId);
    if (!text) {
        return std::unexpected{text.error()};
    }
    auto story = parseNewGameStory(*text);
    if (!story) {
        return std::unexpected{story.error()};
    }
    return std::optional{std::move(*story)};
}

} // namespace dwe::assets
