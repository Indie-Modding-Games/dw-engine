#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

class VirtualFileSystem;

// `data/new_game_story` (content/schemas/new_game_story.schema.json): where the story stands when a new game gives
// the player control. All 800 triggers start clear (script_interp.c: initializeScripts); the new-game section of
// MAPHEAD and the first map's entry section may set some (formats::script::findNewGameTriggers).
struct NewGameStory {
    std::string map;                       // the first map's id
    std::int32_t script = 0;               // the script MAPHEAD loads on it
    std::vector<std::int32_t> triggersSet; // sorted
    // False when the paths of a new game end with different triggers set: triggersSet holds the ones they share.
    bool agreed = true;
    // Sorted: the triggers the game code may have set before the new-game section runs; neither set nor clear.
    std::vector<std::int32_t> triggersUnknown;

    friend bool operator==(const NewGameStory&, const NewGameStory&) = default;
};

// Parses the text of `data/new_game_story`. Unknown and missing keys are errors.
core::Result<NewGameStory> parseNewGameStory(std::string_view yaml);

// Reads and parses `data/new_game_story` from the virtual file system; nothing when no pack has it.
core::Result<std::optional<NewGameStory>> loadNewGameStory(const VirtualFileSystem& vfs);

} // namespace dwe::assets
