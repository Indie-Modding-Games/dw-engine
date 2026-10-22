#pragma once

#include <dwe/core/error.hpp>
#include <dwe/script/api_registry.hpp>

#include <cstddef>
#include <span>
#include <string_view>

namespace dwe::script {

// What firing a hook did.
struct HookReport {
  std::size_t handlersRun = 0;
  // The handlers that raised an error, each logged with its pack's id.
  std::size_t handlersFailed = 0;
  // The handlers not run because their pack spent its hook budget of this event or of this frame.
  std::size_t handlersSkipped = 0;
};

// Named events that packs' scripts handle. The module that owns an event declares it
// ("field.map_entered") and fires it; a pack's script handles it with
// `hooks.on("field.map_entered", function(...) end)`. The handlers run in the order the packs'
// environments were made, and each pack's in the order it registered them. An error in one is
// logged with its pack's id without stopping the others. The handlers of one pack share a budget
// per fired event (ScriptLimits::instructionsPerHook, hookDeadline) and one per frame
// (hookInstructionsPerFrame, hookTimePerFrame): once a pack spends either, its remaining handlers
// are skipped, so a hot handler cannot stall the frame. The module that runs the frames calls
// beginFrame() once per frame. Get one from ScriptVm::hooks(); it must not outlive the VM.
class HookRegistry {
public:
  explicit HookRegistry(detail::VmState& vm) : vm_{&vm} {}

  // Declares an event: a namespace and a name joined by a dot, each of a-z, 0-9 and _ starting with
  // a letter.
  core::Result<void> declare(std::string_view event);

  [[nodiscard]] bool isDeclared(std::string_view event) const;

  // Calls every handler of a declared event with the arguments.
  core::Result<HookReport> fire(std::string_view event, std::span<const ApiValue> arguments = {});

  // Starts a new frame: every pack gets its whole hook budget per frame again.
  void beginFrame();

private:
  detail::VmState* vm_;
};

} // namespace dwe::script
