#include "dev_tools.hpp"

#include <algorithm>
#include <array>

namespace dwe::app {
namespace {

// The console commands the developer menu's pages write (ui/dev_menu/logic.lua).
constexpr std::array<std::string_view, 8> MenuCommands{
    "give_item", "story_set", "partner_set", "clock_speed", "evolve", "warp", "sleep", "toilet"};

constexpr std::string_view ArgumentOpen = "(\"";
constexpr std::string_view ArgumentClose = "\")";

} // namespace

bool devMenuLineAllowed(std::string_view line) {
  const std::size_t open = line.find(ArgumentOpen);
  if (open == std::string_view::npos || !line.ends_with(ArgumentClose)) {
    return false;
  }
  const std::string_view name = line.substr(0, open);
  if (std::ranges::find(MenuCommands, name) == MenuCommands.end()) {
    return false;
  }
  // The argument is plain text: no quote or backslash that could close the string and go on with
  // more code, nor a line break.
  const std::string_view argument = line.substr(
      open + ArgumentOpen.size(), line.size() - open - ArgumentOpen.size() - ArgumentClose.size());
  return std::ranges::none_of(argument, [](char character) {
    return character == '"' || character == '\\' || character == '\n' || character == '\r' ||
           character == ']';
  });
}

} // namespace dwe::app
