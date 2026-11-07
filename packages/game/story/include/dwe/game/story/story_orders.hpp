#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/story/section_host.hpp>
#include <dwe/game/story/story_ports.hpp>

#include <cstdint>

// The story orders that change only the story's state (script_interp.c: scriptInstruction10to27 and
// scriptInstruction28to3F; docs/reference/scripts.md). Operands are plain integers: the original's
// operand widths are machinery, not rules. What the player can see is kept as a rule of StoryRules:
// the range of a variable's value.
namespace dwe::game::story {

// The game rules of the story's state, from the ruleset.
struct StoryRules {
  // A base variable (BaseVariableCount) holds 0 to this; a write past either end stops there. The
  // original's is 255.
  std::int64_t variableMax = 0;
  // The same for the ruleset's extra variables (StoryCapacity), which mods use as plain integers.
  std::int64_t extraVariableMax = 0;
};

class StoryOrders {
public:
  // The rules must give a base variable range past 0.
  StoryOrders(StoryStatePort& state, StoryRandomPort& random, StoryRules rules);

  // set_trigger and unset_trigger.
  core::Result<void> setTrigger(std::int64_t trigger);
  core::Result<void> unsetTrigger(std::int64_t trigger);
  // set_pstat: the variable takes the value, kept in the variable range.
  core::Result<void> setVariable(std::int64_t variable, std::int64_t value);
  // add_to_pstat and reduce_pstat: adds or subtracts, and the result stops at the ends of the
  // variable range.
  core::Result<void> addToVariable(std::int64_t variable, std::int64_t amount);
  core::Result<void> reduceVariable(std::int64_t variable, std::int64_t amount);
  // copy_pstat: the target variable takes the source's value, kept in the target's range.
  core::Result<void> copyVariable(std::int64_t source, std::int64_t target);
  // store_random: the variable takes a random number from 0 to `highest`, which stops at the ends
  // of the variable range, drawn as math.c: randomLimit draws it.
  core::Result<void> storeRandom(std::int64_t variable, std::int64_t highest);
  // delay: the wait of `frames` frames, at least one (script_textbox.c: tickTextboxHandling).
  static FrameWait delay(std::int64_t frames);
  // warp_to: the map change to `map` at `exit`, then the section `arrival` of the new map's script,
  // or none with NoArrivalSection.
  static core::Result<MapChangeWait> warpTo(std::int64_t map, std::int64_t exit,
                                            std::int64_t arrival);
  // teleport: the map change to the map in `variable` at the exit in the next variable, with no
  // section on arrival.
  [[nodiscard]] core::Result<MapChangeWait> teleport(std::int64_t variable) const;

private:
  [[nodiscard]] core::Result<void> checkTrigger(std::int64_t trigger) const;
  [[nodiscard]] core::Result<void> checkVariable(std::int64_t variable) const;
  [[nodiscard]] core::Result<std::int64_t> readVariable(std::int64_t variable) const;
  core::Result<void> writeVariable(std::int64_t variable, std::int64_t value);
  // The highest value of a variable: the base set's range or the extra variables'.
  [[nodiscard]] std::int64_t highestOf(std::int64_t variable) const;
  [[nodiscard]] std::int64_t inVariableRange(std::int64_t variable, std::int64_t value) const;

  StoryStatePort* state_;
  StoryRandomPort* random_;
  StoryRules rules_;
};

} // namespace dwe::game::story
