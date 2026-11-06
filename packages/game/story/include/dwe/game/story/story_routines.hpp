#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/story/story_task.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <variant>

// call_routine: the story's numbered routines (script_interp.c: scriptInstruction64to7E,
// SCRIPT_OP_CALL_ROUTINE, and tickScript for the ones that wait). Each system registers the
// routines it owns; the section host publishes the order and drives the routines that wait.
namespace dwe::game::story {

using RoutineNumber = std::int64_t;

// The routine is done: the section goes on in the same frame (the original's `break`).
struct RoutineGoesOn {};
// The section waits one frame, then goes on (the original's longjmp without CALL_ROUTINE).
struct RoutineYields {};
// The section waits for the task: a menu or a scene that ticks once a frame from the next one.
struct RoutineWaits {
  std::shared_ptr<StoryTask> task;
};

using RoutineStart = std::variant<RoutineGoesOn, RoutineYields, RoutineWaits>;

// A routine's handler gets the number it was called with, so a group shares one handler.
using RoutineHandler = std::function<core::Result<RoutineStart>(RoutineNumber routine)>;

class RoutineTable {
public:
  // Fails when the number already has a handler.
  core::Result<void> registerRoutine(RoutineNumber routine, RoutineHandler handler);
  [[nodiscard]] bool registered(RoutineNumber routine) const;
  // Runs the routine's handler. A number with no handler goes on, as the original's switch does
  // for the numbers it does not list, and the log says so.
  [[nodiscard]] core::Result<RoutineStart> call(RoutineNumber routine) const;

private:
  // Wrapped so the map's iterators do not bring std::expected's operator== into lookup: libc++
  // recurses on it when comparing them.
  struct Entry {
    RoutineHandler handler;
  };
  std::map<RoutineNumber, Entry> handlers_;
};

} // namespace dwe::game::story
