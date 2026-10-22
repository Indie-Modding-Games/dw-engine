#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// `data/new_game_story` (content/schemas/new_game_story.schema.json): where the story stands when a
// new game gives the player control. All 800 triggers start clear (script_interp.c:
// initializeScripts); the new-game section of MAPHEAD and the first map's entry section may set
// some (formats::script::findNewGameTriggers). The map change a new game makes before the player
// can move (formats::script::findNewGameOpening): a spawn on a step cell runs its section at once.
// A start elsewhere skips it, and its `sets` with it.
struct NewGameOpening {
  std::int32_t exit = 0;    // the first map's exit the new game spawns at
  std::int32_t section = 0; // the step section of the cell under the spawn
  std::optional<std::string> toMap;
  std::optional<std::int32_t> toScreen; // when no map has the number
  std::int32_t toExit = 0;
  std::vector<std::int32_t> sets; // sorted

  friend bool operator==(const NewGameOpening&, const NewGameOpening&) = default;
};

struct NewGameStory {
  std::string map;                       // the first map's id
  std::int32_t script = 0;               // the script MAPHEAD loads on it
  std::vector<std::int32_t> triggersSet; // sorted
  // False when the paths of a new game end with different triggers set: triggersSet holds the ones
  // they share.
  bool agreed = true;
  // Sorted: the triggers the game code may have set before the new-game section runs; neither set
  // nor clear.
  std::vector<std::int32_t> triggersUnknown;
  std::optional<NewGameOpening> opening;

  friend bool operator==(const NewGameStory&, const NewGameStory&) = default;
};

// Parses the text of `data/new_game_story`. Unknown and missing keys are errors.
core::Result<NewGameStory> parseNewGameStory(std::string_view yaml);

// Reads and parses `data/new_game_story` from the virtual file system; nothing when no pack has it.
core::Result<std::optional<NewGameStory>> loadNewGameStory(const VirtualFileSystem& vfs);

} // namespace dwe::assets
