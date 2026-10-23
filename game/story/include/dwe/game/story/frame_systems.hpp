#pragma once

#include <cstdint>

// What runs inside the story's frame besides the sections: the scripted moves, and later the boxes.
// The original ticks them in tickScript before the waits are checked and the next order runs
// (script_interp.c: tickScript), and only while a script runs.
namespace dwe::game::story {

class ScriptFrameSystem {
public:
  ScriptFrameSystem() = default;
  ScriptFrameSystem(const ScriptFrameSystem&) = delete;
  ScriptFrameSystem& operator=(const ScriptFrameSystem&) = delete;
  ScriptFrameSystem(ScriptFrameSystem&&) = delete;
  ScriptFrameSystem& operator=(ScriptFrameSystem&&) = delete;
  virtual ~ScriptFrameSystem() = default;

  // A script starts (script_interp.c: callScriptSection).
  virtual void scriptBegins() = 0;
  // One frame of the original while a script runs, before its waits are checked.
  virtual void scriptFrame() = 0;
  // The script froze the world for a talk with the character whose script id is `talker`, the
  // section the event started at (script_interp.c: scriptPauseGame).
  virtual void talkFrozen(std::int64_t talker) = 0;
};

} // namespace dwe::game::story
