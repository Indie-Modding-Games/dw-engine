#pragma once

#include <dwe/game/story/section_host.hpp>
#include <dwe/game/story/story_runner.hpp>
#include <dwe/game/story/story_state.hpp>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dwe::app {

// The field map's camera as the control channel's story_state shows it, in world space
// (docs/architecture/world-space.md): the fixed eye and target, the projection's focal length in
// background pixels, the whole background's size, and the window of it the viewport shows.
struct StoryCameraView {
  glm::vec3 eye{0.0F};
  glm::vec3 target{0.0F};
  float focalLength = 0.0F;
  glm::vec2 backgroundSize{0.0F};
  glm::vec2 viewport{0.0F};
  float scale = 1.0F;
  glm::vec2 scroll{0.0F};
};

// What story_state reports (remote_control.hpp): the story's triggers and variables, the script
// that runs, and the camera.
struct StoryStateView {
  std::string map;
  game::story::StoryState state;
  // A script runs or is about to (StoryRunner::busy), the map's script set_script set up, and the
  // sections running now, the first requested first, with the lines of code they stand at.
  bool busy = false;
  std::optional<std::uint16_t> mapScript;
  std::vector<game::story::StoryRunner::RunningSectionLines> sections;
  // The story lock's stage (session::StoryLock), empty when none.
  std::string stage;
  // None while no map is shown.
  std::optional<StoryCameraView> camera;
};

// The view as one JSON object: {"map", "triggers": the set ones, ascending, "variables": the
// non-zero ones by number ({"12": 3}), "script": {"busy", "map_script", "sections": [{"script",
// "section", "lines": the innermost first}]}, "stage", "camera": {"eye", "target", "focal_length",
// "background", "viewport", "scale", "scroll"}}. Absent values are null.
std::string storyStateJson(const StoryStateView& view);

} // namespace dwe::app
