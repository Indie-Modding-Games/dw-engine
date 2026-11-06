#pragma once

#include <array>
#include <optional>
#include <span>
#include <string_view>

// The names the story API gives the listing's ops and operands (docs/modding/historia.md#nombres).
// The listing keeps the names its YAML contract has; the Lua that dwx writes uses these, which
// never name a franchise, console or company.
namespace dwe::formats::script {

// The words no generated name may contain, in lowercase. The one list: tests and tools read it from
// here.
inline constexpr std::array<std::string_view, 7> ForbiddenNameWords = {
    "digimon", "playstation", "psx", "ps1", "sony", "bandai", "microsoft"};

// A listing name and the name the story API gives it.
struct StoryRename {
  std::string_view listing;
  std::string_view story;
};

// Every op whose story function has another name than the op.
std::span<const StoryRename> storyFunctionRenames();
// Every operand whose name in the story API differs from the listing's.
std::span<const StoryRename> storyOperandRenames();

// The story function that runs an op: story.<name>.
std::string_view storyFunctionName(std::string_view op);
// The name of an operand in the comments of the Lua.
std::string_view storyOperandName(std::string_view operand);

// The first forbidden word a text contains, ignoring case.
std::optional<std::string_view> forbiddenWordIn(std::string_view text);

} // namespace dwe::formats::script
