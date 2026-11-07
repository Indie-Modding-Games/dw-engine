#pragma once

#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::session {

// The ruleset section of the story lock (rules/<id>/story_lock.yaml): which part of the game is
// open while the port grows stage by stage. A ruleset without it locks nothing.
inline constexpr std::string_view StoryLockSection = "story_lock";

// The stage that opens everything (the --story-stage launch option), for development and tests; no
// stage may take this id.
inline constexpr std::string_view StoryStageAll = "all";

// A map pattern: a map id, or a prefix ending in '*' ("twnb*"); "*" alone is every map.
inline constexpr char MapPatternWildcard = '*';

// The destinations a map's exits may lead to; the map's other exits stay closed.
struct StoryLockExits {
  std::string map;
  std::vector<std::string> open;
};

// The cell sections of a map's script the story may run; the map's other cells stay closed.
struct StoryLockSections {
  std::string map;
  std::vector<std::int32_t> open;
};

// A stage: the maps it opens, and its frontier: the exits and cell sections narrowed inside the
// open maps while it is the active stage.
struct StoryLockStage {
  std::string id;
  std::vector<std::string> maps;
  std::vector<StoryLockExits> exits;
  std::vector<StoryLockSections> sections;
};

// The stages in delivery order. Stage N opens the maps of stages 1 to N; only the active stage's
// frontier applies, since the next stage moves it.
struct StoryLockRules {
  // The latest stage delivered.
  std::string activeStage;
  // The text key of the message a closed exit shows.
  std::string closedMessage;
  std::vector<StoryLockStage> stages;
};

// Reads the section; errors name the file, line and column. The active stage must be one of the
// stages, and the stage ids are unique and never StoryStageAll.
core::Result<StoryLockRules> readStoryLockRules(const ruleset::Section& section);

// Whether a map is one a pack adds (`pack:name`): the lock follows the original's story, so it
// never closes those, nor an exit into one.
[[nodiscard]] bool isPackMap(std::string_view map);

// Whether a map pattern matches a map id.
[[nodiscard]] bool mapPatternMatches(std::string_view pattern, std::string_view map);

// Answers what a stage keeps open. Without a stage (no section, or StoryStageAll) everything is
// open. Only new map changes are asked: a player already on a closed map (a save from further on)
// stays there and may leave by any exit into an open map.
class StoryLock {
public:
  StoryLock() = default;
  // `stage` must be one of the rules' stages.
  StoryLock(const StoryLockRules& rules, std::string_view stage);

  [[nodiscard]] bool locks() const { return !stage_.empty(); }
  [[nodiscard]] std::string_view stage() const { return stage_; }
  [[nodiscard]] std::string_view closedMessage() const { return closedMessage_; }

  // Whether the player may be taken to this map.
  [[nodiscard]] bool mapOpen(std::string_view map) const;
  // Whether an exit of `from` may lead to `to`: the destination is open and, when the stage
  // narrows the exits of `from`, it is among them.
  [[nodiscard]] bool exitOpen(std::string_view from, std::string_view to) const;
  // Whether the story may run this cell section of the map's script.
  [[nodiscard]] bool sectionOpen(std::string_view map, std::int32_t section) const;

private:
  std::string stage_;
  // The maps of the stages up to the active one, and the active stage's frontier.
  std::vector<std::string> maps_;
  std::vector<StoryLockExits> exits_;
  std::vector<StoryLockSections> sections_;
  std::string closedMessage_;
};

// The lock of the ruleset's section at `stage`: empty takes the section's active_stage, and
// StoryStageAll opens everything. A ruleset without the section locks nothing. A stage the section
// does not have is an error.
core::Result<StoryLock> storyLockOf(const ruleset::Ruleset& rules, std::string_view stage);

// The check of the section for the ruleset store.
ruleset::SectionChecks storyLockSectionChecks();

} // namespace dwe::session
