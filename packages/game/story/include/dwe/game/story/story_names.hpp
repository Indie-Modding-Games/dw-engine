#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

// The names the tools show for the story's triggers and variables (`story/names` in the game pack,
// content/schemas/story_names.schema.json). The story runs on numbers; a name is only a label, an
// id in snake_case, for the mapper, the remote control's dumps and the logs.
namespace dwe::game::story {

struct StoryNames {
  std::map<std::int32_t, std::string> triggers;
  std::map<std::int32_t, std::string> variables;

  // The name of a trigger or a variable, or nothing.
  [[nodiscard]] const std::string* trigger(std::int32_t trigger) const;
  [[nodiscard]] const std::string* variable(std::int32_t variable) const;
};

// Parses the text of `story/names`: numbers within the state (TriggerCount, VariableCount), ids
// of lower-case letters, digits and '_', each name once per kind. Unknown keys are errors.
core::Result<StoryNames> parseStoryNames(std::string_view yaml);

// Reads `story/names` from the virtual file system; without one, no names.
core::Result<StoryNames> loadStoryNames(const assets::VirtualFileSystem& vfs);

} // namespace dwe::game::story
