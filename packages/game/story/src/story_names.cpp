#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/story/story_names.hpp>
#include <dwe/game/story/story_state.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <format>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "messages.hpp"

namespace dwe::game::story {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view NamesId = "story/names";

bool isNameId(std::string_view text) {
  return !text.empty() && std::ranges::all_of(text, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
  });
}

std::map<std::int32_t, std::string> readNames(YamlReader& reader, const YamlNode& node,
                                              std::string_view kind, std::int32_t count) {
  std::map<std::int32_t, std::string> names;
  if (!node.isMapping()) {
    reader.fail(node, std::format("{} must be a mapping of numbers to names", kind));
    return names;
  }
  std::set<std::string> seen;
  for (const core::YamlEntry& entry : node.entries()) {
    std::int32_t number = 0;
    const char* end = entry.key.data() + entry.key.size();
    const auto [stop, error] = std::from_chars(entry.key.data(), end, number);
    if (error != std::errc{} || stop != end || number < 0 || number >= count) {
      reader.fail(entry.value,
                  messages::formatMessage(messages::NameNumberOutside, kind, entry.key, count - 1));
      return names;
    }
    std::string name = reader.text(entry.value);
    if (!reader.ok()) {
      return names;
    }
    if (!isNameId(name)) {
      reader.fail(entry.value, messages::formatMessage(messages::NameNotAnId, name));
      return names;
    }
    if (!seen.insert(name).second) {
      reader.fail(entry.value, messages::formatMessage(messages::NameTwice, kind, name));
      return names;
    }
    names.emplace(number, std::move(name));
  }
  return names;
}

const std::string* nameIn(const std::map<std::int32_t, std::string>& names, std::int32_t number) {
  const auto found = names.find(number);
  return found != names.end() ? &found->second : nullptr;
}

} // namespace

const std::string* StoryNames::trigger(std::int32_t trigger) const {
  return nameIn(triggers, trigger);
}

const std::string* StoryNames::variable(std::int32_t variable) const {
  return nameIn(variables, variable);
}

core::Result<StoryNames> parseStoryNames(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{core::Error{std::format("story names: {}", tree.error().message)}};
  }
  YamlReader reader;
  if (!reader.expectKeys(*tree, "the story names", {}, {"triggers", "variables"})) {
    return std::unexpected{reader.error("story names")};
  }
  StoryNames names;
  if (const YamlNode* triggers = tree->find("triggers"); triggers != nullptr) {
    names.triggers = readNames(reader, *triggers, "triggers", BaseTriggerCount);
  }
  if (const YamlNode* variables = tree->find("variables"); variables != nullptr) {
    names.variables = readNames(reader, *variables, "variables", BaseVariableCount);
  }
  return reader.finish(std::move(names), "story names");
}

core::Result<StoryNames> loadStoryNames(const assets::VirtualFileSystem& vfs) {
  const auto id = assets::AssetId::parse(NamesId);
  if (!id) {
    return std::unexpected{id.error()};
  }
  if (vfs.resolve(*id) == nullptr) {
    return StoryNames{};
  }
  const auto bytes = vfs.read(*id);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  return parseStoryNames(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
}

} // namespace dwe::game::story
