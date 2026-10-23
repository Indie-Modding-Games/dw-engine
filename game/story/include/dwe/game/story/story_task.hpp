#pragma once

#include <dwe/game/story/section_host.hpp>

#include <variant>

// The waits another system ends: a battle, a routine with a menu or a scene (script_interp.c:
// tickScript keeps ACTIVE_INSTRUCTION at CALL_ROUTINE and ticks the routine once a frame until it
// says it is done; start_battle runs the battle and then reads its outcome). The section host
// drives a task: it ticks it when the order yields and then once a frame, waiting a frame at a
// time, and turns its end into the section's next step.
namespace dwe::game::story {

// The task goes on: the section keeps waiting this frame.
struct TaskRunning {};

// The task is over: the section goes on in the same frame and gets `value` back.
struct TaskDone {
  ResumeValue value;
};

// The task moves the script pointer to another section, as jump_to_file does (script_interp.c:
// scriptInstruction64to7E, a lost battle goes on at MAPHEAD 0x4DE).
struct TaskJump {
  SectionRef target;
};

// The task changes the map: the section ends and the map's chain runs on the new map, with no
// arrival section unless the change names one (a fled battle goes back to the previous screen).
struct TaskMapChange {
  MapChangeWait change;
};

using TaskStep = std::variant<TaskRunning, TaskDone, TaskJump, TaskMapChange>;

class StoryTask {
public:
  StoryTask() = default;
  StoryTask(const StoryTask&) = delete;
  StoryTask& operator=(const StoryTask&) = delete;
  StoryTask(StoryTask&&) = delete;
  StoryTask& operator=(StoryTask&&) = delete;
  virtual ~StoryTask() = default;

  // Once in the frame the section yields, then once a frame until the task leaves TaskRunning.
  virtual TaskStep tick() = 0;
};

// A task that is done the first time it ticks: an order that goes on in the same frame.
class DoneTask final : public StoryTask {
public:
  explicit DoneTask(ResumeValue value = {}) : value_{value} {}
  TaskStep tick() override { return TaskDone{.value = value_}; }

private:
  ResumeValue value_;
};

// A task whose first tick is in the frame after the order, as the original's routines tick from
// the next tickScript on: `step` runs from then on.
class NextFrameTask : public StoryTask {
public:
  TaskStep tick() final {
    if (!started_) {
      started_ = true;
      return TaskRunning{};
    }
    return step();
  }

protected:
  virtual TaskStep step() = 0;

private:
  bool started_ = false;
};

} // namespace dwe::game::story
