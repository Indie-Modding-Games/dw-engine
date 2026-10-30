#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/story/prosperity.hpp>
#include <dwe/session/session_system.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dwe::session {

class StoryAdapter;

// The ruleset section of the city.
inline constexpr std::string_view CitySection = "city";

struct CityRules {
  game::story::ProsperityRules prosperity;
  // The File City screens a finished game continues on, first match wins.
  std::vector<game::story::CityMapChoice> clearedMap;
};

// Reads the city section; errors name the file and line.
core::Result<CityRules> readCityRules(const ruleset::Section& section);

// The city's prosperity (main.c: recalculatePPandArena): rebuilt from the recruit triggers when a
// new game starts, after a load and before each of the player's saves, as the original does when
// its game loop starts and in writeSavegame. The scripts read it and the triggers to grow the city.
class CityAdapter final : public SessionSystem {
public:
  // The story must outlive the adapter; its rules must be read before the city's.
  explicit CityAdapter(StoryAdapter& story);

  [[nodiscard]] ruleset::SectionChecks sectionChecks() const override;
  // Reads the city section; without one the prosperity is left as the scripts make it.
  core::Result<void> readRules(const ruleset::Ruleset& rules) override;
  // Must tick after the story's start.
  core::Result<void> startNewGame(GameSession& session) override;
  void beforeSave(GameSession& session) override;
  void afterLoad(GameSession& session) override;

  // Rebuilds the prosperity now; nothing without the rules.
  void recalculate();
  // The File City screen a game saved after the ending continues on (map.c: getFileCityTopMap).
  [[nodiscard]] std::optional<std::int32_t> clearedMap() const;

private:
  StoryAdapter* story_;
  std::optional<CityRules> rules_;
  // Each species' level by its number, from the story's digimon table.
  std::unordered_map<std::int32_t, std::int32_t> levels_;
};

} // namespace dwe::session
