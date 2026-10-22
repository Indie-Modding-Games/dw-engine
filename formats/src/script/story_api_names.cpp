#include <dwe/formats/script/story_api_names.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace dwe::formats::script {

namespace {

// The engine's words for the same things: a species (content ids, data/digimon) and the partner.
// What each op does is in script_interp.c: scriptInstruction10to27 (0x22), scriptInstruction28to3F
// (0x3F), scriptInstruction46to58 (0x46, 0x47, 0x49) and scriptInstruction64to7E (0x79).
constexpr std::array FunctionRenames{
    StoryRename{.listing = "store_digimon_type", .story = "store_partner_species"},
    StoryRename{.listing = "store_digimon_value", .story = "store_species_type"},
    StoryRename{.listing = "load_digimon", .story = "load_species_model"},
    StoryRename{.listing = "set_digimon", .story = "spawn_npc"},
    StoryRename{.listing = "call_digimon_routine", .story = "call_partner_routine"},
    StoryRename{.listing = "unload_digimon", .story = "unload_species_model"},
};

constexpr std::array OperandRenames{
    StoryRename{.listing = "digimon", .story = "species"},
};

std::string_view renamed(std::span<const StoryRename> renames, std::string_view name) {
  const auto found = std::ranges::find(renames, name, &StoryRename::listing);
  return found != renames.end() ? found->story : name;
}

} // namespace

std::span<const StoryRename> storyFunctionRenames() {
  return FunctionRenames;
}

std::span<const StoryRename> storyOperandRenames() {
  return OperandRenames;
}

std::string_view storyFunctionName(std::string_view op) {
  return renamed(FunctionRenames, op);
}

std::string_view storyOperandName(std::string_view operand) {
  return renamed(OperandRenames, operand);
}

std::optional<std::string_view> forbiddenWordIn(std::string_view text) {
  std::string lower(text.size(), ' ');
  // ASCII only, so that the locale does not change what is found.
  std::ranges::transform(text, lower.begin(), [](char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  });
  const auto found = std::ranges::find_if(
      ForbiddenNameWords, [&](std::string_view word) { return lower.contains(word); });
  return found != ForbiddenNameWords.end() ? std::optional{*found} : std::nullopt;
}

} // namespace dwe::formats::script
