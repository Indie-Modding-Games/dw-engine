#include "new_game_mode.hpp"

#include <dwe/ruleset/ruleset.hpp>

#include <algorithm>
#include <string>

namespace dwe::app {

namespace {

constexpr std::string_view ModeList = "new_game_mode";
// The ruleset with the original's hidden content, which the rules screen offers.
constexpr std::string_view HiddenContentRuleset = "oculto";

bool offers(const game::modes::GameMode& mode, std::string_view ruleset) {
  return std::ranges::find(mode.rulesets, ruleset) != mode.rulesets.end();
}

} // namespace

screens::ScreenSettings NewGameModes::settings() const {
  const auto modes = catalog_.modes();
  const std::size_t shown = std::min(modes.size(), ShownModes);
  screens::ScreenSettings settings{
      {.name = std::string{ModeList} + ".count", .value = static_cast<double>(shown)}};
  for (std::size_t index = 0; index < shown; ++index) {
    const game::modes::GameMode& mode = modes[index];
    settings.push_back({.name = screens::listEntryName(ModeList, index, "id"), .value = mode.id});
    settings.push_back(
        {.name = screens::listEntryName(ModeList, index, "name"), .value = mode.nameKey});
    settings.push_back({.name = screens::listEntryName(ModeList, index, "description"),
                        .value = mode.descriptionKey});
  }
  return settings;
}

NewGameModes::Next NewGameModes::choose(std::string_view id, PlayStart& start) const {
  const game::modes::GameMode* mode = catalog_.find(id);
  if (mode == nullptr) {
    return Next::Unknown;
  }
  start.modeId = mode->id;
  if (offers(*mode, ruleset::OriginalRuleset) && offers(*mode, HiddenContentRuleset)) {
    return Next::AskRules;
  }
  start.rulesetId = mode->ruleset;
  return Next::Start;
}

} // namespace dwe::app
