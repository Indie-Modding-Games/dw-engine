#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::app {

// The naming box (script_menu.c: setupNameSelectorBox, tickNamingBox): how many letters a name
// takes and the letters' grid, row by row, `columns` letters each (a space is an empty cell). The
// first half of the rows is the grid's left side, the second half its right side.
struct NamingRules {
  std::size_t length = 0;
  std::size_t columns = 0;
  std::vector<std::string> rows;
};

// The old Digimon of the new game's opening (new_game.c: loadNewGameScene, tickNewGameJijimon):
// its species, where it stands in the original's axes, the clip it starts with, the frame it turns
// to the player on and the clip it plays then, and its turn: from `yaw` to `turnedYaw`, in 4096ths
// of a turn as the original.
struct InterviewHost {
  std::string species;
  glm::vec3 position{0.0F};
  std::uint32_t clip = 0;
  std::uint32_t turnFrame = 0;
  std::uint32_t turnedClip = 0;
  std::int32_t yaw = 0;
  std::int32_t turnedYaw = 0;
};

// The new_game_scene section (rules/<id>/new_game_scene.yaml): the opening's questions before the
// first map (main.c: newGameScene, script_menu.c: newGameStateMachine).
struct NewGameSceneRules {
  // MAPHEAD's line table of the opening's lines (resolveMapHeadEntry(0x4D6, ...)).
  std::int32_t lines = 0;
  // The partner each answer picks, by the answer's number (main.c: main, readPStat(0xFE)).
  std::vector<std::string> partners;
  // The fade in at the start and the fade out at the end, in frames (fadeFromBlack(0x14),
  // fadeToBlack(0x28)).
  std::uint32_t fadeInFrames = 0;
  std::uint32_t fadeOutFrames = 0;
  // Engine ticks per frame of the scene (its loop waits VSync(3)).
  std::uint32_t frameTicks = 1;
  // How far in front of the origin the camera is (GsSetRefView2: vpz -0x1068).
  float cameraDistance = 0.0F;
  // The projection distance over the original's 240 lines (GsSetProjection), which gives the field
  // of view, and the line the projection is centred on (DRAWING_OFFSET_Y).
  float cameraProjection = 0.0F;
  float cameraCentreLine = 0.0F;
  InterviewHost host;
  NamingRules naming;
};

inline constexpr std::string_view NewGameSceneSection = "new_game_scene";

core::Result<NewGameSceneRules> readNewGameSceneRules(const ruleset::Section& section);

// The section of a ruleset of the packs; nothing when the ruleset has none (a new game then starts
// without the opening's questions) or it cannot be read (with a warning).
std::optional<NewGameSceneRules> loadNewGameSceneRules(const assets::VirtualFileSystem& files,
                                                       std::string_view rulesetId);

} // namespace dwe::app
