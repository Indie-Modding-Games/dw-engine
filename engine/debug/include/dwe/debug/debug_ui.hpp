#pragma once

#include <dwe/core/error.hpp>
#include <dwe/debug/tuning.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/platform/window.hpp>
#include <dwe/rhi/device.hpp>
#include <dwe/script/script_vm.hpp>

#include <filesystem>
#include <memory>

union SDL_Event;

namespace dwe::debug {

struct FrameStats {
  double framesPerSecond;
  double frameMilliseconds;
  int ticksThisFrame;
};

// A circle in window coordinates. Phase 0 placeholder that shows scripted movement until the render
// module exists.
struct Marker {
  float x;
  float y;
  float radius;
};

// The developer overlay, drawn with Dear ImGui: the log with a Lua command line, frame statistics,
// the state of every input device and an asset viewer. Toggled with the debug_console action; never
// part of the game's own UI.
class DebugUi {
public:
  static core::Result<std::unique_ptr<DebugUi>> create(const platform::Window& window,
                                                       rhi::Device& device);

  DebugUi(const DebugUi&) = delete;
  DebugUi& operator=(const DebugUi&) = delete;
  DebugUi(DebugUi&&) = delete;
  DebugUi& operator=(DebugUi&&) = delete;
  ~DebugUi();

  void handleRawEvent(const SDL_Event& event);

  // True while the overlay is visible and using the keyboard (typing a command), so the game
  // ignores the keys.
  [[nodiscard]] bool capturesKeyboard() const;

  // True while the overlay is visible and the pointer is over it, so clicks on it do not reach the
  // game.
  [[nodiscard]] bool capturesMouse() const;

  void toggle();

  // Mounts the packs of a content folder in the asset viewer: the folder itself if it is a pack,
  // otherwise the packs right under it.
  void mountContent(const std::filesystem::path& folder);

  // Gives the tuning panel (R3.4) the game's packs, ruleset store and clock. The panel opens from
  // the Frame window or with the console command /tuning.
  void attachTuning(const TuningTarget& target);

  // Adds a system's tab to the tuning panel, after attachTuning.
  void addTuningTab(TuningTab tab);

  // Removes the tuning panel before what it tunes goes; the panel stays open for the next
  // attachTuning.
  void detachTuning();

  // Shows the overlay with the tuning panel open, now or once attachTuning gives it one.
  void openTuning();

  // Builds the overlay for this frame and records it into the frame, which must be inside
  // rendering.
  void draw(const rhi::Frame& frame, const FrameStats& stats, const input::InputState& input,
            script::ScriptVm& scripts, const Marker& marker);

private:
  struct Impl;

  explicit DebugUi(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

} // namespace dwe::debug
