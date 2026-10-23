#include <dwe/formats/script/lua_emitter.hpp>
#include <dwe/formats/script/story_api_names.hpp>

#include <yaml/yaml_values.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "listing_vocabulary.hpp"

namespace dwe::formats::script {

namespace {

// The stat that script_value.c: scriptCompareStat compares as a signed 16-bit value
// (SCRIPT_STAT_HAPPINESS).
constexpr std::int32_t SignedStat = 9;
constexpr std::int64_t Unsigned16 = 0x10000;
constexpr std::int64_t Signed16Limit = 0x8000;
constexpr std::int64_t Unsigned32 = 0x100000000;
constexpr std::size_t IndentWidth = 2;

constexpr std::array<std::string_view, 6> CompareOperators = {"==", "~=", ">=", "<=", ">", "<"};

std::string hex4(std::uint32_t value) {
  return std::format("{:04x}", value);
}

std::string routineName(std::uint32_t offset) {
  return "routines.at_" + hex4(offset);
}

// A Lua string literal: printable ASCII as is, the rest as decimal escapes, so the module is plain
// ASCII.
std::string luaString(std::string_view text) {
  std::string out = "\"";
  for (const char c : text) {
    const auto byte = static_cast<unsigned char>(c);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (byte >= 0x20 && byte < 0x7F) {
      out += c;
    } else {
      out += std::format("\\{:03d}", byte);
    }
  }
  return out + "\"";
}

bool isIdentifier(std::string_view name) {
  if (name.empty() || (name.front() >= '0' && name.front() <= '9')) {
    return false;
  }
  return std::ranges::all_of(name, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
  });
}

std::optional<std::int32_t> conditionArgument(const Condition& condition, std::string_view name) {
  const auto found = std::ranges::find(condition.arguments, name, &Argument::name);
  return found == condition.arguments.end() ? std::nullopt : std::optional{found->value};
}

std::string_view compareOperator(std::int32_t compare) {
  if (compare < 0 || std::cmp_greater_equal(compare, CompareOperators.size())) {
    return {};
  }
  return CompareOperators[static_cast<std::size_t>(compare)];
}

// A boolean Lua expression, and whether it is a combination that needs parentheses inside another
// one.
struct Expression {
  std::string text;
  bool combined = false;
  bool alwaysFalse = false;
};

class ModuleWriter {
public:
  ModuleWriter(const ScriptListing& listing, const StructuredScript& script,
               const LuaModuleName& name)
      : listing_(&listing), script_(&script), name_(&name) {}

  core::Result<LuaModule> write() {
    line(std::format("{}{}: script {}, from {}", core::story_modules::HeaderPrefix,
                     LuaEmitterVersion, name_->script, name_->source));
    line("local story <const> = story");
    line("local ENDED <const> = story.ENDED");
    line("local PLAYER <const> = story.PLAYER");
    line("local PARTNER <const> = story.PARTNER");
    line("");
    line("local routines = {}");
    std::set<std::size_t> written;
    for (const StructuredRoutine& routine : script_->routines) {
      if (!written.insert(routine.instruction).second) {
        continue;
      }
      if (routine.instruction >= listing_->instructions.size()) {
        return fail(
            std::format("routine at instruction {} is not in the listing", routine.instruction));
      }
      line("");
      line(std::format("-- {}", describe(routine)));
      line(std::format("function {}()", routineName(offsetOf(routine.instruction))),
           offsetOf(routine.instruction));
      ++depth_;
      writeList(routine.body);
      --depth_;
      line("end");
    }
    line("");
    line("return {");
    ++depth_;
    line(std::format("script = {},", name_->script));
    line("sections = {");
    ++depth_;
    // getScriptSection takes the first entry of the table with the id, so a later one with the same
    // id is never run by it; a Lua table would keep the last.
    std::map<std::uint16_t, std::uint32_t> firstEntry;
    for (const ListingSection& section : listing_->sections) {
      const auto index = listing_->indexAt(section.entry);
      if (!index || !written.contains(*index)) {
        return fail(std::format("section {:#06x} has no routine", section.id));
      }
      if (const auto [first, added] = firstEntry.try_emplace(section.id, section.entry); !added) {
        line(std::format("-- [0x{}] = {}: the game runs the one at 0x{} instead", hex4(section.id),
                         routineName(section.entry), hex4(first->second)));
        continue;
      }
      line(std::format("[0x{}] = {},", hex4(section.id), routineName(section.entry)));
    }
    --depth_;
    line("},");
    --depth_;
    line("}");
    if (error_) {
      return core::fail(*error_);
    }
    std::ranges::sort(module_.texts, {}, &LuaText::offset);
    const auto [first, last] = std::ranges::unique(module_.texts, {}, &LuaText::offset);
    module_.texts.erase(first, last);
    return std::move(module_);
  }

private:
  static core::Result<LuaModule> fail(std::string message) {
    return core::fail(std::move(message));
  }

  void setError(std::string message) {
    if (!error_) {
      error_ = std::move(message);
    }
  }

  [[nodiscard]] bool startsRoutine(std::uint32_t offset) const {
    const auto index = listing_->indexAt(offset);
    return index && std::ranges::any_of(script_->routines, [&](const StructuredRoutine& routine) {
             return routine.instruction == *index;
           });
  }

  [[nodiscard]] std::uint32_t offsetOf(std::size_t instruction) const {
    return listing_->instructions[instruction].offset;
  }

  [[nodiscard]] static std::string describe(const StructuredRoutine& routine) {
    std::string kind;
    switch (routine.kind) {
    case RoutineKind::Section:
      kind = std::format("section 0x{}", hex4(routine.section));
      break;
    case RoutineKind::Subroutine:
      kind = "subroutine";
      break;
    case RoutineKind::SharedTail:
      kind = "shared tail";
      break;
    }
    if (routine.fallback) {
      switch (*routine.fallback) {
      case FallbackReason::Irreducible:
        kind += ", a state machine: a loop with two ways in";
        break;
      case FallbackReason::NestedExit:
        kind += ", a state machine: a jump out of two loops";
        break;
      case FallbackReason::Duplication:
        kind += ", a state machine: too much shared code to copy";
        break;
      }
    }
    return kind;
  }

  void line(std::string_view text, std::optional<std::uint32_t> offset = std::nullopt) {
    if (!text.empty()) {
      module_.code.append(static_cast<std::size_t>(depth_) * IndentWidth, ' ');
      module_.code += text;
    }
    module_.code += '\n';
    ++lineNumber_;
    if (offset) {
      module_.lines.push_back({.line = lineNumber_, .offset = *offset});
    }
  }

  // A return or a call that ends the routine: Lua takes a return only as the last statement of a
  // block. A trailing comment, if any, goes after the whole line.
  void finalLine(std::string_view text, std::uint32_t offset, bool last) {
    const std::size_t comment = text.find(" -- ");
    const std::string_view statement = text.substr(0, comment);
    const std::string_view rest =
        comment == std::string_view::npos ? std::string_view{} : text.substr(comment);
    if (last) {
      line(text, offset);
    } else {
      line(std::format("do {} end{}", statement, rest), offset);
    }
  }

  void writeList(const StatementList& list) {
    for (std::size_t index = 0; index < list.size(); ++index) {
      const bool last = index + 1 == list.size();
      std::visit([&](const auto& node) { this->write(node, last); }, list[index].node);
    }
  }

  // ---- runs

  void write(const RunStatement& run, bool last) {
    for (std::size_t index = run.first; index < run.first + run.count; ++index) {
      if (index >= listing_->instructions.size()) {
        setError(std::format("a run names instruction {}, past the listing", index));
        return;
      }
      writeInstruction(listing_->instructions[index], last && index + 1 == run.first + run.count);
    }
  }

  // The operands of a call: pads and labels left out, an entity by its name.
  static std::string operands(const ListingInstruction& instruction) {
    std::string out;
    for (const Argument& argument : instruction.arguments) {
      if (argument.kind == ArgumentKind::Pad || argument.kind == ArgumentKind::Label) {
        continue;
      }
      if (!out.empty()) {
        out += ", ";
      }
      if (argument.kind == ArgumentKind::Entity && argument.value == vocabulary::PlayerEntity) {
        out += "PLAYER";
      } else if (argument.kind == ArgumentKind::Entity &&
                 argument.value == vocabulary::PartnerEntity) {
        out += "PARTNER";
      } else {
        out += std::to_string(argument.value);
      }
    }
    return out;
  }

  // The operand names after a call with more than one, so the line reads without the listing.
  static std::string operandNames(const ListingInstruction& instruction) {
    std::vector<std::string_view> names;
    for (const Argument& argument : instruction.arguments) {
      if (argument.kind != ArgumentKind::Pad && argument.kind != ArgumentKind::Label) {
        names.push_back(storyOperandName(argument.name));
      }
    }
    if (names.size() < 2) {
      return {};
    }
    std::string out = " -- ";
    for (std::size_t index = 0; index < names.size(); ++index) {
      out += (index == 0 ? "" : ", ") + std::string{names[index]};
    }
    return out;
  }

  static std::string call(const ListingInstruction& instruction) {
    return std::format("story.{}({})", storyFunctionName(instruction.op), operands(instruction));
  }

  std::string textKey(const ListingInstruction& instruction) {
    const std::string key = luaTextKey(name_->textSource, instruction.offset);
    module_.texts.push_back(
        {.offset = instruction.offset, .key = key, .text = instruction.text.value_or("")});
    return luaString(key);
  }

  void writeInstruction(const ListingInstruction& instruction, bool last) {
    const std::uint32_t at = instruction.offset;
    if (!opcodeNamed(instruction.op) || !isIdentifier(instruction.op)) {
      line(
          std::format("error({})", luaString(std::format("{} {:#06x}: {} is not an instruction the "
                                                         "interpreter has",
                                                         name_->source, at, instruction.op))),
          at);
      return;
    }
    switch (instruction.role) {
    case FlowRole::Plain:
      if (instruction.text) {
        line(std::format("story.{}({})", storyFunctionName(instruction.op), textKey(instruction)),
             at);
      } else {
        line(call(instruction) + operandNames(instruction), at);
      }
      return;
    case FlowRole::LoadScript:
      line(call(instruction) + operandNames(instruction), at);
      return;
    case FlowRole::Call:
      if (instruction.targets.empty() || !startsRoutine(instruction.targets.front())) {
        setError(std::format("the jump_and_link at {:#06x} does not go to a subroutine", at));
        return;
      }
      line(std::format("if {}() == ENDED then return ENDED end -- {}",
                       routineName(instruction.targets.front()), storyFunctionName(instruction.op)),
           at);
      return;
    case FlowRole::FileCall:
      line(std::format("if {} == ENDED then return ENDED end", call(instruction)) +
               operandNames(instruction),
           at);
      return;
    case FlowRole::FileJump:
    case FlowRole::MapChange:
      finalLine("return " + call(instruction), at, last);
      return;
    case FlowRole::Return:
      finalLine("return -- " + std::string{storyFunctionName(instruction.op)}, at, last);
      return;
    case FlowRole::EndSection:
    case FlowRole::EndScript:
      finalLine("return ENDED -- " + std::string{storyFunctionName(instruction.op)}, at, last);
      return;
    case FlowRole::Data:
      line(std::format("error({})", luaString(std::format("{} {:#06x}: a data block is not code",
                                                          name_->source, at))),
           at);
      return;
    case FlowRole::Jump:
    case FlowRole::If:
    case FlowRole::Switch:
    case FlowRole::Selection:
      // The structure stands for these; they are never in a run.
      setError(std::format("a run holds the {} at {:#06x}", instruction.op, at));
      return;
    }
  }

  // ---- conditions

  // The test of one IF entry, in the domain script_value.c compares it in.
  std::string test(const Condition& condition, std::uint32_t at) {
    const auto value = conditionArgument(condition, "value");
    const auto compare = [&](const std::string& read, std::int64_t constant) {
      const std::string_view op =
          compareOperator(conditionArgument(condition, "compare").value_or(-1));
      if (op.empty()) {
        setError(
            std::format("the IF at {:#06x} has a comparison the interpreter does not have", at));
      }
      return std::format("{} {} {}", read, op, constant);
    };
    const auto negated = [&](const std::string& read) {
      return conditionArgument(condition, "negate").value_or(0) != 0 ? "not " + read : read;
    };
    switch (condition.test) {
    case ConditionTest::Trigger: {
      const std::string read =
          std::format("story.trigger({})", conditionArgument(condition, "trigger").value_or(0));
      return (condition.code & vocabulary::LowBits) != 0 ? "not " + read : read;
    }
    case ConditionTest::Pstat: {
      const std::string_view op = compareOperator(condition.code & vocabulary::LowBits);
      if (op.empty()) {
        setError(
            std::format("the IF at {:#06x} has a comparison the interpreter does not have", at));
      }
      return std::format("story.var({}) {} {}", conditionArgument(condition, "pstat").value_or(0),
                         op, value.value_or(0));
    }
    case ConditionTest::Stat: {
      const std::int32_t stat = conditionArgument(condition, "stat").value_or(0);
      std::int64_t constant = value.value_or(0) & (Unsigned16 - 1);
      if (stat == SignedStat && constant >= Signed16Limit) {
        constant -= Unsigned16;
      }
      return compare(std::format("story.stat({})", stat), constant);
    }
    case ConditionTest::Card:
      return compare(
          std::format("story.cards({})", conditionArgument(condition, "card").value_or(0)),
          value.value_or(0) & (Unsigned16 - 1));
    case ConditionTest::ItemCount:
      return compare(
          std::format("story.items({})", conditionArgument(condition, "item").value_or(0)),
          value.value_or(0) & (Unsigned16 - 1));
    case ConditionTest::Money: {
      std::int64_t constant = value.value_or(0);
      if (constant < 0) {
        constant += Unsigned32;
      }
      return compare("story.money()", constant);
    }
    case ConditionTest::Move:
      return negated(
          std::format("story.knows_move({})", conditionArgument(condition, "move").value_or(0)));
    case ConditionTest::Condition:
      return negated(
          std::format("story.has_condition({})", conditionArgument(condition, "mask").value_or(0)));
    case ConditionTest::GotoIfTrue:
    case ConditionTest::GotoIfFalse:
      break;
    }
    return "false";
  }

  // The result of an IF's entries before `entry`, folded left to right from false (script_interp.c:
  // scriptIfInstruction): AND and OR combine with the result so far, any other test replaces it,
  // and the jump entries leave it as it is. The reads are pure, so Lua's short circuit gives the
  // same result.
  Expression fold(const BranchCondition& condition) {
    if (condition.instruction >= listing_->instructions.size()) {
      setError(
          std::format("a branch names instruction {}, past the listing", condition.instruction));
      return {.text = "false", .alwaysFalse = true};
    }
    const ListingInstruction& instruction = listing_->instructions[condition.instruction];
    Expression result{.text = "false", .combined = false, .alwaysFalse = true};
    const std::size_t end = std::min(condition.entry, instruction.conditions.size());
    for (std::size_t index = 0; index < end; ++index) {
      const Condition& entry = instruction.conditions[index];
      if (entry.test == ConditionTest::GotoIfTrue || entry.test == ConditionTest::GotoIfFalse) {
        continue;
      }
      const std::string next = test(entry, instruction.offset);
      switch (entry.combine) {
      case Combine::Replace:
        result = {.text = next, .combined = false, .alwaysFalse = false};
        break;
      case Combine::And:
        if (!result.alwaysFalse) {
          result = {.text = std::format("{} and {}", wrap(result), next), .combined = true};
        }
        break;
      case Combine::Or:
        result =
            result.alwaysFalse
                ? Expression{.text = next, .combined = false, .alwaysFalse = false}
                : Expression{.text = std::format("{} or {}", wrap(result), next), .combined = true};
        break;
      }
    }
    return result;
  }

  static std::string wrap(const Expression& expression) {
    return expression.combined ? "(" + expression.text + ")" : expression.text;
  }

  // ---- structured statements

  void write(const IfStatement& statement, bool /*last*/) {
    const Expression condition = fold(statement.condition);
    const std::uint32_t at = statement.condition.instruction < listing_->instructions.size()
                                 ? offsetOf(statement.condition.instruction)
                                 : 0;
    if (statement.whenTrue.empty() && !statement.whenFalse.empty()) {
      line(std::format("if not ({}) then", condition.text), at);
      ++depth_;
      writeList(statement.whenFalse);
      --depth_;
      line("end");
      return;
    }
    line(std::format("if {} then", condition.text), at);
    ++depth_;
    writeList(statement.whenTrue);
    --depth_;
    if (!statement.whenFalse.empty()) {
      line("else");
      ++depth_;
      writeList(statement.whenFalse);
      --depth_;
    }
    line("end");
  }

  void write(const LoopStatement& statement, bool /*last*/) {
    line("while true do");
    ++depth_;
    loops_.push_back({.machine = false, .name = {}});
    writeList(statement.body);
    loops_.pop_back();
    --depth_;
    line("end");
  }

  // A break leaves the innermost loop. Inside a state machine's state, which is a loop of its own
  // in Lua, it leaves the machine first: the state becomes nil, and the code after the machine's
  // loop breaks again.
  void write(const BreakStatement& /*statement*/, bool /*last*/) {
    if (loops_.empty()) {
      setError("a break outside every loop");
      return;
    }
    Loop& innermost = loops_.back();
    if (innermost.machine) {
      innermost.breaksOut = true;
      line(innermost.name + " = nil");
    }
    line("break");
  }

  void write(const ChoiceStatement& statement, bool /*last*/) {
    if (statement.instruction >= listing_->instructions.size()) {
      setError(
          std::format("a choice names instruction {}, past the listing", statement.instruction));
      return;
    }
    const ListingInstruction& instruction = listing_->instructions[statement.instruction];
    const std::uint32_t at = instruction.offset;
    std::string value;
    std::size_t cases = statement.arms.size();
    line("do");
    ++depth_;
    if (instruction.role == FlowRole::Selection) {
      // The options are the flow's, as the arms are.
      line(std::format("local choice <const> = story.{}({}, {})", storyFunctionName(instruction.op),
                       textKey(instruction), instruction.targets.size()),
           at);
      value = "choice";
    } else {
      if (statement.arms.empty()) {
        line(std::format("error({})", luaString(std::format("{} {:#06x}: a switch with no cases",
                                                            name_->source, at))),
             at);
        --depth_;
        line("end");
        return;
      }
      // A value past the last case runs the last arm (script_interp.c: scriptInstruction10to27).
      line(std::format("local value <const> = story.var({})",
                       instruction.argument("pstat").value_or(0)),
           at);
      value = "value";
    }
    for (std::size_t arm = 0; arm < cases; ++arm) {
      const bool lastArm = arm + 1 == cases;
      if (arm == 0 && lastArm) {
        line("do");
      } else if (arm == 0) {
        line(std::format("if {} == {} then", value, arm));
      } else if (lastArm) {
        line("else");
      } else {
        line(std::format("elseif {} == {} then", value, arm));
      }
      ++depth_;
      writeList(statement.arms[arm]);
      --depth_;
    }
    line("end");
    --depth_;
    line("end");
  }

  void write(const TailCallStatement& statement, bool last) {
    if (statement.instruction >= listing_->instructions.size()) {
      setError(
          std::format("a tail call names instruction {}, past the listing", statement.instruction));
      return;
    }
    const bool known =
        std::ranges::any_of(script_->routines, [&](const StructuredRoutine& routine) {
          return routine.instruction == statement.instruction;
        });
    if (!known) {
      setError(std::format("a tail call names instruction {}, which starts no routine",
                           statement.instruction));
      return;
    }
    const std::uint32_t at = offsetOf(statement.instruction);
    finalLine(std::format("return {}()", routineName(at)), at, last);
  }

  void write(const StateMachineStatement& machine, bool /*last*/) {
    const std::string name = std::format("state{}", ++machines_);
    line("do");
    ++depth_;
    line(std::format("local {} = {}", name, machine.initial));
    line("while true do");
    ++depth_;
    loops_.push_back({.machine = true, .name = name});
    for (std::size_t index = 0; index < machine.states.size(); ++index) {
      const MachineState& state = machine.states[index];
      // The machine runs states by their place in `states`, which is what a state change names.
      line(std::format("{}if {} == {} then", index == 0 ? "" : "else", name, index));
      ++depth_;
      writeList(state.body);
      --depth_;
    }
    if (!machine.states.empty()) {
      line("end");
    }
    const bool breaksOut = loops_.back().breaksOut;
    loops_.pop_back();
    --depth_;
    line("end");
    if (breaksOut) {
      line(std::format("if {} == nil then", name));
      ++depth_;
      write(BreakStatement{}, true);
      --depth_;
      line("end");
    }
    --depth_;
    line("end");
    --machines_;
  }

  void write(const SetStateStatement& statement, bool /*last*/) {
    if (loops_.empty() || !loops_.back().machine) {
      setError("a state change outside a state machine");
      return;
    }
    line(std::format("{} = {}", loops_.back().name, statement.state));
  }

  struct Loop {
    bool machine = false;
    std::string name; // a machine's state variable
    bool breaksOut = false;
  };

  const ScriptListing* listing_;
  const StructuredScript* script_;
  const LuaModuleName* name_;
  LuaModule module_;
  std::size_t lineNumber_ = 0;
  int depth_ = 0;
  int machines_ = 0;
  std::vector<Loop> loops_;
  std::optional<std::string> error_;
};

} // namespace

std::string luaTextKey(std::string_view textSource, std::uint32_t offset) {
  return std::format("{}.text.{}", textSource, hex4(offset));
}

core::Result<LuaModule> emitLuaModule(const ScriptListing& listing, const StructuredScript& script,
                                      const LuaModuleName& name) {
  return ModuleWriter{listing, script, name}.write();
}

core::Result<LuaModule> convertScriptToLua(const ScriptListing& listing,
                                           const LuaModuleName& name) {
  const auto script = structureScript(listing);
  if (!script) {
    return std::unexpected{script.error()};
  }
  return emitLuaModule(listing, *script, name);
}

core::YamlNode luaLinesToYaml(const LuaModule& module) {
  core::YamlNode lines = core::YamlNode::mapping();
  for (const LuaLine& line : module.lines) {
    lines.set(std::to_string(line.line),
              core::YamlNode::scalar(std::format("{:#06x}", line.offset)));
  }
  return lines;
}

core::YamlNode luaTextsToYaml(const LuaModule& module) {
  core::YamlNode texts = core::YamlNode::mapping();
  for (const LuaText& text : module.texts) {
    texts.set(std::format("text.{}", hex4(text.offset)), yaml::quoted(text.text));
  }
  return texts;
}

} // namespace dwe::formats::script
