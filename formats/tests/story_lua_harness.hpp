#pragma once

// Runs an event script two ways and records what each does, for the Lua emitter's parity tests: the
// listing with a reference interpreter that follows script_interp.c (tickScript,
// scriptIfInstruction, scriptInstruction10to27), and the emitted Lua module in a Lua state with a
// fake `story` API. Both read the same fixed world and the same selection picks, and give the calls
// they made in order ("play_sound(1,2)") and how the run ended.

#include <dwe/formats/script/lua_emitter.hpp>
#include <dwe/formats/script/script_listing.hpp>
#include <dwe/formats/script/story_api_names.hpp>

#include <lua.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace story_lua {

using dwe::formats::script::ConditionTest;
using dwe::formats::script::FlowRole;
using dwe::formats::script::ListingInstruction;
using dwe::formats::script::ScriptListing;

constexpr std::size_t TriggerCount = 800;
constexpr std::size_t TableSize = 256;
constexpr std::int32_t SignedStat = 9;
constexpr std::uint32_t TamerEntity = 0xFD;
constexpr std::uint32_t PartnerEntity = 0xFC;
// How many calls a run records before it is cut.
constexpr std::size_t EventLimit = 200;
// How long a run goes before it is cut: how many instructions the reference runs and how many Lua
// VM instructions the module runs. A run is cut by these only in a loop that makes few calls or
// none.
struct RunLimits {
  std::size_t referenceSteps = 0;
  int luaInstructions = 0;
  // Whether two cut runs must make the same calls. The reference counts listing instructions and
  // Lua counts VM instructions, so with low limits a loop is cut after different numbers of rounds
  // on each side; then two cut runs agree when the calls of one start the calls of the other.
  bool exactCuts = false;
};
// What every commit runs. A loop that makes a call less often than about every 25 reference steps
// is cut before EventLimit calls, so only the calls both sides made before their cut are compared.
constexpr RunLimits FastLimits{
    .referenceSteps = 5000, .luaInstructions = 25000, .exactCuts = false};
// High enough that a random listing's loop with a call reaches EventLimit calls on both sides
// before either limit, so cut runs must match call for call, as must the status.
constexpr RunLimits FullLimits{
    .referenceSteps = 100000, .luaInstructions = 1000000, .exactCuts = true};
constexpr std::size_t PickCount = EventLimit + 1;

// What the reads of the IF tests see, the same for a whole run.
struct World {
  std::vector<bool> triggers = std::vector<bool>(TriggerCount);
  std::array<std::uint32_t, TableSize> vars{};
  std::array<std::int32_t, TableSize> stats{}; // 0..65535, but -32768..32767 for stat 9
  std::array<std::uint32_t, TableSize> cards{};
  std::array<std::uint32_t, TableSize> items{};
  std::array<bool, TableSize> moves{};
  std::uint32_t condition = 0;
  std::uint32_t money = 0;
  std::vector<std::uint32_t> picks; // the selection picks, in order: option pick % (count + 1)
};

class Random {
public:
  explicit Random(std::uint32_t seed) : state_((seed * 2654435761U) + 1U) {}

  std::uint32_t next(std::uint32_t bound) {
    state_ = (state_ * 1664525U) + 1013904223U;
    return (state_ >> 8U) % bound;
  }

private:
  std::uint32_t state_;
};

// A world of small values, so the comparisons of a random listing go both ways.
inline World randomWorld(std::uint32_t seed) {
  Random random{seed};
  World world;
  for (std::size_t index = 0; index < TriggerCount; ++index) {
    world.triggers[index] = random.next(2) == 0;
  }
  constexpr std::array<std::int32_t, 4> UnsignedValues = {0, 1, 2, 0xFFFF};
  constexpr std::array<std::int32_t, 4> SignedValues = {-2, -1, 0, 1};
  for (std::size_t index = 0; index < TableSize; ++index) {
    world.vars[index] = random.next(4);
    world.stats[index] = UnsignedValues.at(random.next(4));
    world.cards[index] = random.next(3);
    world.items[index] = random.next(3);
    world.moves[index] = random.next(2) == 0;
  }
  world.stats[SignedStat] = SignedValues.at(random.next(4));
  world.condition = random.next(TableSize);
  constexpr std::array<std::uint32_t, 4> Money = {0, 1, 2, 0xFFFFFFFFU};
  world.money = Money.at(random.next(4));
  for (std::size_t index = 0; index < PickCount; ++index) {
    world.picks.push_back(random.next(1U << 16U));
  }
  return world;
}

struct Outcome {
  std::vector<std::string> events;
  std::string
      status; // "ended", "returned" (a jump_return with nothing to return to), "cut" or "error"

  bool operator==(const Outcome&) const = default;
};

// Two runs agree when they are equal or, with limits that are not exactCuts, when both were cut and
// the calls of one start the calls of the other.
inline bool sameRun(const Outcome& actual, const Outcome& expected, const RunLimits& limits) {
  if (limits.exactCuts || actual.status != "cut" || expected.status != "cut") {
    return actual == expected;
  }
  const std::size_t common = std::min(actual.events.size(), expected.events.size());
  return std::equal(actual.events.begin(),
                    actual.events.begin() + static_cast<std::ptrdiff_t>(common),
                    expected.events.begin());
}

inline std::string describe(const Outcome& outcome) {
  std::string out = outcome.status + ":";
  for (const std::string& event : outcome.events) {
    out += " " + event;
  }
  return out;
}

// ---- the reference interpreter

inline bool compareUnsigned(std::int32_t op, std::uint32_t lhs, std::uint32_t rhs) {
  switch (op & 7) {
  case 0:
    return lhs == rhs;
  case 1:
    return lhs != rhs;
  case 2:
    return lhs >= rhs;
  case 3:
    return lhs <= rhs;
  case 4:
    return lhs > rhs;
  case 5:
    return lhs < rhs;
  default:
    return false;
  }
}

inline bool compareSigned(std::int32_t op, std::int32_t lhs, std::int32_t rhs) {
  switch (op & 7) {
  case 0:
    return lhs == rhs;
  case 1:
    return lhs != rhs;
  case 2:
    return lhs >= rhs;
  case 3:
    return lhs <= rhs;
  case 4:
    return lhs > rhs;
  case 5:
    return lhs < rhs;
  default:
    return false;
  }
}

inline std::int32_t operand(const dwe::formats::script::Condition& condition,
                            std::string_view name) {
  const auto found =
      std::ranges::find(condition.arguments, name, &dwe::formats::script::Argument::name);
  return found == condition.arguments.end() ? 0 : found->value;
}

inline std::size_t slot(std::int32_t value) {
  return static_cast<std::size_t>(value) % TableSize;
}

// script_interp.c: scriptIfInstruction and script_value.c: scriptCompare*.
inline bool evaluate(const dwe::formats::script::Condition& condition, const World& world) {
  const auto compare = operand(condition, "compare");
  const auto value = operand(condition, "value");
  switch (condition.test) {
  case ConditionTest::Trigger: {
    const bool set = world.triggers.at(static_cast<std::size_t>(operand(condition, "trigger")));
    return (condition.code & 7U) == 0 ? set : !set;
  }
  case ConditionTest::Pstat:
    return compareUnsigned(condition.code, world.vars.at(slot(operand(condition, "pstat"))),
                           static_cast<std::uint32_t>(value));
  case ConditionTest::Stat: {
    const auto stat = operand(condition, "stat");
    if (stat == SignedStat) {
      return compareSigned(compare, world.stats.at(slot(stat)),
                           static_cast<std::int16_t>(static_cast<std::uint16_t>(value)));
    }
    return compareUnsigned(compare, static_cast<std::uint16_t>(world.stats.at(slot(stat))),
                           static_cast<std::uint16_t>(value));
  }
  case ConditionTest::Card:
    return compareUnsigned(compare, world.cards.at(slot(operand(condition, "card"))),
                           static_cast<std::uint16_t>(value));
  case ConditionTest::ItemCount:
    return compareUnsigned(compare, world.items.at(slot(operand(condition, "item"))),
                           static_cast<std::uint16_t>(value));
  case ConditionTest::Money:
    return compareUnsigned(compare, world.money, static_cast<std::uint32_t>(value));
  case ConditionTest::Move:
    return world.moves.at(slot(operand(condition, "move"))) != (operand(condition, "negate") != 0);
  case ConditionTest::Condition:
    return ((world.condition & static_cast<std::uint32_t>(operand(condition, "mask"))) != 0) !=
           (operand(condition, "negate") != 0);
  case ConditionTest::GotoIfTrue:
  case ConditionTest::GotoIfFalse:
    break;
  }
  return false;
}

// The call the module makes for an instruction: its op and operands, pads and labels left out.
inline std::string callOf(const ListingInstruction& instruction, std::string_view textSource) {
  std::string out = std::string{dwe::formats::script::storyFunctionName(instruction.op)} + "(";
  bool first = true;
  const auto add = [&](const std::string& text) {
    out += (first ? "" : ",") + text;
    first = false;
  };
  if (instruction.role == FlowRole::Selection) {
    add(dwe::formats::script::luaTextKey(textSource, instruction.offset));
    add(std::to_string(instruction.targets.size()));
    return out + ")";
  }
  if (instruction.text) {
    add(dwe::formats::script::luaTextKey(textSource, instruction.offset));
    return out + ")";
  }
  for (const auto& argument : instruction.arguments) {
    if (argument.kind != dwe::formats::script::ArgumentKind::Pad &&
        argument.kind != dwe::formats::script::ArgumentKind::Label) {
      add(std::to_string(argument.value));
    }
  }
  return out + ")";
}

inline Outcome runReference(const ScriptListing& listing, std::size_t entry, const World& world,
                            std::string_view textSource, const RunLimits& limits) {
  Outcome outcome;
  std::vector<std::size_t> stack;
  std::size_t picked = 0;
  std::size_t pc = entry;
  const auto at = [&](std::uint32_t offset) {
    return listing.indexAt(offset).value_or(listing.instructions.size());
  };
  for (std::size_t step = 0; step < limits.referenceSteps; ++step) {
    if (pc >= listing.instructions.size()) {
      outcome.status = "error";
      return outcome;
    }
    const ListingInstruction& instruction = listing.instructions[pc];
    const auto record = [&] {
      if (outcome.events.size() >= EventLimit) {
        return false;
      }
      outcome.events.push_back(callOf(instruction, textSource));
      return true;
    };
    switch (instruction.role) {
    case FlowRole::Plain:
    case FlowRole::LoadScript:
    case FlowRole::FileCall:
      if (!record()) {
        outcome.status = "cut";
        return outcome;
      }
      ++pc;
      break;
    case FlowRole::FileJump:
    case FlowRole::MapChange:
      outcome.status = record() ? "ended" : "cut";
      return outcome;
    case FlowRole::Call:
      stack.push_back(pc + 1);
      pc = at(instruction.targets.front());
      break;
    case FlowRole::Return:
      if (stack.empty()) {
        outcome.status = "returned";
        return outcome;
      }
      pc = stack.back();
      stack.pop_back();
      break;
    case FlowRole::Jump:
      pc = at(instruction.targets.front());
      break;
    case FlowRole::If: {
      bool result = false;
      std::optional<std::size_t> next;
      for (std::size_t index = 0; index < instruction.conditions.size() && !next; ++index) {
        const auto& condition = instruction.conditions[index];
        if (condition.test == ConditionTest::GotoIfTrue ||
            condition.test == ConditionTest::GotoIfFalse) {
          if (result == (condition.test == ConditionTest::GotoIfTrue)) {
            const auto jump = std::ranges::find(instruction.conditionalJumps, index,
                                                &dwe::formats::script::ConditionalJump::entry);
            next = at(jump->target);
          }
          continue;
        }
        const bool holds = evaluate(condition, world);
        switch (condition.combine) {
        case dwe::formats::script::Combine::And:
          result = result && holds;
          break;
        case dwe::formats::script::Combine::Or:
          result = result || holds;
          break;
        case dwe::formats::script::Combine::Replace:
          result = holds;
          break;
        }
      }
      pc = next.value_or(pc + 1);
      break;
    }
    case FlowRole::Switch: {
      if (instruction.targets.empty()) {
        outcome.status = "error";
        return outcome;
      }
      const std::size_t value = world.vars.at(slot(instruction.argument("pstat").value_or(0)));
      pc = at(instruction.targets[std::min(value, instruction.targets.size() - 1)]);
      break;
    }
    case FlowRole::Selection: {
      if (!record()) {
        outcome.status = "cut";
        return outcome;
      }
      const std::size_t pick = world.picks.at(picked++) % (instruction.targets.size() + 1);
      pc = pick < instruction.targets.size() ? at(instruction.targets[pick]) : pc + 1;
      break;
    }
    case FlowRole::EndSection:
    case FlowRole::EndScript:
      outcome.status = "ended";
      return outcome;
    case FlowRole::Data:
      outcome.status = "error";
      return outcome;
    }
  }
  outcome.status = "cut";
  return outcome;
}

// ---- the module in Lua

// The fake `story` API: the reads answer from `world`, every other function records its call.
// jump_to_file, warp_to and teleport end the script, as the runtime's do.
inline constexpr std::string_view FakeStory = R"lua(
local world, limit = ...
local events = {}
local picked = 0
local function record(name, ...)
  if #events >= limit then error("limit", 0) end
  local parts = {}
  for index = 1, select("#", ...) do parts[index] = tostring((select(index, ...))) end
  events[#events + 1] = name .. "(" .. table.concat(parts, ",") .. ")"
end
local story = { ENDED = true, TAMER = 0xFD, PARTNER = 0xFC }
function story.trigger(n) return world.triggers[n] end
function story.var(n) return world.vars[n] end
function story.stat(n) return world.stats[n] end
function story.cards(n) return world.cards[n] end
function story.items(n) return world.items[n] end
function story.knows_move(n) return world.moves[n] end
function story.has_condition(mask) return (world.condition & mask) ~= 0 end
function story.money() return world.money end
function story.set_selection(key, count)
  record("set_selection", key, count)
  picked = picked + 1
  return world.picks[picked] % (count + 1)
end
local ends = { jump_to_file = true, warp_to = true, teleport = true }
setmetatable(story, { __index = function(_, name)
  return function(...)
    record(name, ...)
    if ends[name] then return true end
    return nil
  end
end })
story_events = events
return story
)lua";

struct StateCloser {
  void operator()(lua_State* state) const { lua_close(state); }
};
using LuaState = std::unique_ptr<lua_State, StateCloser>;

inline void stopRun(lua_State* state, lua_Debug* /*debug*/) {
  lua_pushliteral(state, "limit");
  lua_error(state);
}

inline void pushArray(lua_State* state, const auto& values) {
  lua_createtable(state, 0, static_cast<int>(values.size()));
  for (std::size_t index = 0; index < values.size(); ++index) {
    if constexpr (std::is_same_v<std::decay_t<decltype(values[index])>, bool>) {
      lua_pushboolean(state, values[index] ? 1 : 0);
    } else {
      lua_pushinteger(state, static_cast<lua_Integer>(values[index]));
    }
    lua_rawseti(state, -2, static_cast<lua_Integer>(index));
  }
}

inline void pushWorld(lua_State* state, const World& world) {
  lua_createtable(state, 0, 9);
  std::vector<bool> triggers = world.triggers;
  lua_createtable(state, 0, static_cast<int>(triggers.size()));
  for (std::size_t index = 0; index < triggers.size(); ++index) {
    lua_pushboolean(state, triggers[index] ? 1 : 0);
    lua_rawseti(state, -2, static_cast<lua_Integer>(index));
  }
  lua_setfield(state, -2, "triggers");
  pushArray(state, world.vars);
  lua_setfield(state, -2, "vars");
  pushArray(state, world.stats);
  lua_setfield(state, -2, "stats");
  pushArray(state, world.cards);
  lua_setfield(state, -2, "cards");
  pushArray(state, world.items);
  lua_setfield(state, -2, "items");
  pushArray(state, world.moves);
  lua_setfield(state, -2, "moves");
  lua_pushinteger(state, world.condition);
  lua_setfield(state, -2, "condition");
  lua_pushinteger(state, world.money);
  lua_setfield(state, -2, "money");
  lua_createtable(state, static_cast<int>(world.picks.size()), 0);
  for (std::size_t index = 0; index < world.picks.size(); ++index) {
    lua_pushinteger(state, world.picks[index]);
    lua_rawseti(state, -2, static_cast<lua_Integer>(index) + 1);
  }
  lua_setfield(state, -2, "picks");
}

// Whether Lua compiles a module, without running it; the message when it does not.
inline std::optional<std::string> compileError(std::string_view code, std::string_view name) {
  LuaState state{luaL_newstate()};
  if (luaL_loadbufferx(state.get(), code.data(), code.size(), std::string{name}.c_str(), "t") !=
      LUA_OK) {
    return std::string{lua_tostring(state.get(), -1)};
  }
  return std::nullopt;
}

// Runs one section of a module: loads the fake API, runs the module and calls sections[section].
inline Outcome runModule(std::string_view code, std::uint16_t section, const World& world,
                         const RunLimits& limits) {
  LuaState owner{luaL_newstate()};
  lua_State* state = owner.get();
  luaL_openlibs(state);
  Outcome outcome;
  if (luaL_loadbufferx(state, FakeStory.data(), FakeStory.size(), "fake story", "t") != LUA_OK) {
    outcome.status = std::string{"fake story: "} + lua_tostring(state, -1);
    return outcome;
  }
  pushWorld(state, world);
  lua_pushinteger(state, static_cast<lua_Integer>(EventLimit));
  if (lua_pcall(state, 2, 1, 0) != LUA_OK) {
    outcome.status = std::string{"fake story: "} + lua_tostring(state, -1);
    return outcome;
  }
  lua_setglobal(state, "story");
  if (luaL_loadbufferx(state, code.data(), code.size(), "module", "t") != LUA_OK ||
      lua_pcall(state, 0, 1, 0) != LUA_OK) {
    outcome.status = std::string{"module: "} + lua_tostring(state, -1);
    return outcome;
  }
  lua_getfield(state, -1, "sections");
  lua_rawgeti(state, -1, section);
  lua_sethook(state, stopRun, LUA_MASKCOUNT, limits.luaInstructions);
  const int status = lua_pcall(state, 0, 1, 0);
  lua_sethook(state, nullptr, 0, 0);
  if (status == LUA_OK) {
    outcome.status = lua_toboolean(state, -1) != 0 ? "ended" : "returned";
  } else {
    const std::string message = lua_tostring(state, -1) != nullptr ? lua_tostring(state, -1) : "";
    outcome.status = message == "limit" || message.contains("stack overflow") ? "cut" : "error";
  }
  lua_settop(state, 0);
  lua_getglobal(state, "story_events");
  const lua_Integer count = luaL_len(state, -1);
  for (lua_Integer index = 1; index <= count; ++index) {
    lua_rawgeti(state, -1, index);
    outcome.events.emplace_back(lua_tostring(state, -1));
    lua_pop(state, 1);
  }
  return outcome;
}

} // namespace story_lua
