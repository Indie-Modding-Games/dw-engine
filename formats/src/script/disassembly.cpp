#include <dwe/formats/byte_reader.hpp>
#include <dwe/formats/script/disassembly.hpp>
#include <dwe/formats/text/game_text.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <initializer_list>
#include <set>

#include "listing_vocabulary.hpp"

namespace dwe::formats::script {

namespace {

using enum ArgumentKind;

// Where execution can go after an instruction, for the reachability pass.

struct Field {
  ArgumentKind kind = Pad;
  std::string_view name;
};

constexpr std::size_t MaxFields = 8;

struct OpcodeInfo {
  std::uint8_t code = 0;
  std::string_view name;
  FlowRole flow = FlowRole::Plain;
  std::array<Field, MaxFields> fields{};
  std::size_t fieldCount = 0;

  [[nodiscard]] std::span<const Field> layout() const {
    return std::span{fields}.first(fieldCount);
  }
};

constexpr OpcodeInfo op(std::uint8_t code, std::string_view name, FlowRole flow,
                        std::initializer_list<Field> fields) {
  OpcodeInfo info{.code = code, .name = name, .flow = flow, .fieldCount = fields.size()};
  std::ranges::copy(fields, info.fields.begin());
  return info;
}

constexpr Field pad() {
  return {.kind = Pad, .name = ""};
}
constexpr Field u8(std::string_view name) {
  return {.kind = U8, .name = name};
}
constexpr Field u16(std::string_view name) {
  return {.kind = U16, .name = name};
}
constexpr Field s16(std::string_view name) {
  return {.kind = S16, .name = name};
}
constexpr Field s32(std::string_view name) {
  return {.kind = S32, .name = name};
}
constexpr Field pstat(std::string_view name = "pstat") {
  return {.kind = Pstat, .name = name};
}
constexpr Field trigger() {
  return {.kind = Trigger, .name = "trigger"};
}
constexpr Field entity(std::string_view name = "entity") {
  return {.kind = Entity, .name = name};
}
constexpr Field label() {
  return {.kind = Label, .name = "target"};
}

// The opcodes tickScript dispatches (0x10-0x3F, 0x46-0x58, 0x5A-0x5F, 0x64-0x7E, 0xFB-0xFF), named
// after include/dw/script.h. The layouts are the fixed operands; SET_SELECTION, SWITCH, IF,
// SHOW_TEXTBOX and DATA go on with variable parts that decodeInstruction reads.
constexpr std::array Opcodes = {
    op(0x10, "set_selection", FlowRole::Selection, {u8("count")}),
    op(0x11, "unused_11", FlowRole::Plain, {}),
    op(0x12, "unused_12", FlowRole::Plain, {pad()}),
    op(0x13, "jump_and_link", FlowRole::Call, {pad(), label()}),
    op(0x14, "jump_to_file_and_link", FlowRole::FileCall, {pad(), u16("script"), u16("section")}),
    op(0x15, "jump_return", FlowRole::Return, {pad()}),
    op(0x16, "jump_to", FlowRole::Jump, {pad(), label()}),
    op(0x17, "jump_to_file", FlowRole::FileJump, {pad(), u16("script"), u16("section")}),
    op(0x18, "switch", FlowRole::Switch, {pstat(), u16("count")}),
    op(0x19, "if", FlowRole::If, {pad()}),
    op(0x1A, "show_textbox", FlowRole::Plain, {pad()}),
    op(0x1B, "set_dialog_owner", FlowRole::Plain, {entity()}),
    op(0x1C, "set_trigger", FlowRole::Plain, {pad(), trigger()}),
    op(0x1D, "unset_trigger", FlowRole::Plain, {pad(), trigger()}),
    op(0x1E, "set_pstat", FlowRole::Plain, {pad(), pstat(), u8("value")}),
    op(0x1F, "add_to_pstat", FlowRole::Plain, {pad(), pstat(), u8("value")}),
    op(0x20, "reduce_pstat", FlowRole::Plain, {pad(), pstat(), u8("value")}),
    op(0x21, "store_map_id", FlowRole::Plain, {pstat()}),
    op(0x22, "store_digimon_type", FlowRole::Plain, {pstat()}),
    op(0x23, "set_inventory_size", FlowRole::Plain, {u8("size")}),
    op(0x24, "store_random", FlowRole::Plain, {pad(), pstat(), u8("max")}),
    op(0x25, "store_date", FlowRole::Plain, {pstat()}),
    op(0x26, "set_textbox_size", FlowRole::Plain, {entity(), u8("columns"), u8("rows")}),
    op(0x27, "fadeout_hud", FlowRole::Plain, {u8("box")}),
    op(0x28, "give_item", FlowRole::Plain, {pad(), u8("item"), u8("amount")}),
    op(0x29, "remove_item", FlowRole::Plain, {pad(), u8("item"), u8("amount")}),
    op(0x2A, "add_money", FlowRole::Plain, {pad(), s32("amount")}),
    op(0x2B, "reduce_money", FlowRole::Plain, {pad(), s32("amount")}),
    op(0x2C, "compare_date", FlowRole::Plain,
       {pstat(), trigger(), u8("compare"), u8("year"), u8("day"), u8("hour"), u8("minute"), pad()}),
    op(0x2D, "learn_move", FlowRole::Plain, {u8("move")}),
    op(0x2E, "unused_2e", FlowRole::Plain, {u8("unknown01")}),
    op(0x2F, "give_card", FlowRole::Plain, {u8("card")}),
    op(0x30, "take_card", FlowRole::Plain, {u8("card")}),
    op(0x31, "set_merit", FlowRole::Plain, {pad(), u16("merit")}),
    op(0x32, "add_merit", FlowRole::Plain, {pad(), u16("merit")}),
    op(0x33, "reduce_merit", FlowRole::Plain, {pad(), u16("merit")}),
    op(0x34, "set_stat", FlowRole::Plain, {u8("stat"), u16("value")}),
    op(0x35, "add_stat", FlowRole::Plain, {u8("stat"), u16("value")}),
    op(0x36, "reduce_stat", FlowRole::Plain, {u8("stat"), u16("value")}),
    op(0x37, "advance_to_date_at", FlowRole::Plain, {pstat()}),
    op(0x38, "add_minutes_to_date_at", FlowRole::Plain, {pstat(), s32("minutes")}),
    op(0x39, "add_minutes_to_date_at_2", FlowRole::Plain, {pstat(), s32("minutes")}),
    op(0x3A, "unused_3a", FlowRole::Plain, {pad(), u8("unknown02"), u8("unknown03")}),
    op(0x3B, "unused_3b", FlowRole::Plain, {pad(), u8("unknown02"), u8("unknown03")}),
    op(0x3C, "unused_3c", FlowRole::Plain, {pad(), u8("unknown02"), u8("unknown03")}),
    op(0x3D, "unused_3d", FlowRole::Plain, {u8("unknown01")}),
    op(0x3E, "unused_3e", FlowRole::Plain, {u8("unknown01")}),
    op(0x3F, "store_digimon_value", FlowRole::Plain,
       {pad(), pstat("source"), pstat("destination")}),
    op(0x46, "load_digimon", FlowRole::Plain, {u8("digimon")}),
    op(0x47, "set_digimon", FlowRole::Plain, {u8("digimon"), u8("slot"), u8("autotalk")}),
    op(0x48, "unload_entity", FlowRole::Plain, {entity()}),
    op(0x49, "call_digimon_routine", FlowRole::Plain, {u8("routine")}),
    op(0x4A, "wait_for_entity", FlowRole::Plain, {entity()}),
    op(0x4B, "warp_to", FlowRole::MapChange, {u8("screen"), u8("exit"), u8("section")}),
    op(0x4C, "entity_look_at_entity", FlowRole::Plain, {pad(), entity(), entity("target_entity")}),
    op(0x4D, "entity_set_rotation", FlowRole::Plain, {entity(), s16("rotation")}),
    op(0x4E, "entity_walk_to", FlowRole::Plain,
       {entity(), s16("x"), s16("z"), u8("animation"), u8("unknown07")}),
    op(0x4F, "move_camera_to", FlowRole::Plain, {u8("speed"), s16("x"), s16("z")}),
    op(0x50, "move_camera_to_entity", FlowRole::Plain, {pad(), entity(), u8("speed")}),
    op(0x51, "entity_walk_to_entity", FlowRole::Plain,
       {entity(), u8("animation"), entity("target_entity")}),
    op(0x52, "entity_walk_to_with_camera", FlowRole::Plain,
       {entity(), s16("x"), s16("z"), u8("animation"), u8("unknown07")}),
    op(0x53, "entity_walk_to_entity_with_camera", FlowRole::Plain,
       {entity(), u8("animation"), entity("target_entity")}),
    op(0x54, "reset_entity_origin", FlowRole::Plain, {entity()}),
    op(0x55, "set_textbox_origin", FlowRole::Plain, {pad(), s16("x"), s16("y"), s16("z")}),
    op(0x56, "play_animation", FlowRole::Plain, {pad(), entity(), u8("animation")}),
    op(0x57, "set_obj_visibility", FlowRole::Plain, {pad(), u8("object"), u8("visible")}),
    op(0x58, "teleport", FlowRole::MapChange, {pstat()}),
    op(0x5A, "play_sound", FlowRole::Plain, {pad(), u8("bank"), u8("sound")}),
    op(0x5B, "unused_5b", FlowRole::Plain, {u8("unknown01")}),
    op(0x5C, "unused_5c", FlowRole::Plain, {u8("unknown01")}),
    op(0x5D, "set_bgm", FlowRole::Plain, {u8("track")}),
    op(0x5E, "stop_bgm", FlowRole::Plain, {u8("unknown01")}),
    op(0x5F, "unused_5f", FlowRole::Plain, {u8("unknown01")}),
    op(0x64, "call_routine", FlowRole::Plain, {u8("routine")}),
    op(0x65, "remove_condition", FlowRole::Plain, {u8("mask")}),
    op(0x66, "start_battle", FlowRole::Plain, {u8("unknown01")}),
    op(0x67, "delay", FlowRole::Plain, {pad(), u16("frames")}),
    op(0x68, "set_textbox_mode", FlowRole::Plain, {pad(), u8("mode"), u8("frames")}),
    op(0x69, "deal_damage", FlowRole::Plain, {pstat()}),
    op(0x6A, "set_autotalk", FlowRole::Plain, {pad(), entity(), u8("enabled")}),
    op(0x6B, "data", FlowRole::Data, {pad()}),
    op(0x6C, "entity_move_to", FlowRole::Plain,
       {entity(), s16("x"), s16("z"), u8("speed"), u8("unknown07")}),
    op(0x6D, "entity_move_to_entity", FlowRole::Plain,
       {entity(), entity("target_entity"), u8("speed")}),
    op(0x6E, "entity_move_to_with_camera", FlowRole::Plain,
       {entity(), s16("x"), s16("z"), u8("speed"), u8("unknown07")}),
    op(0x6F, "entity_move_to_entity_with_camera", FlowRole::Plain,
       {entity(), entity("target_entity"), u8("speed")}),
    op(0x70, "rotate_3d_object", FlowRole::Plain, {entity(), u8("unknown02"), u8("target")}),
    op(0x71, "move_object_to", FlowRole::Plain,
       {u8("slot"), entity(), u8("speed"), entity("target_entity"), pad(), s16("x"), s16("z")}),
    op(0x72, "entity_move_to_axis", FlowRole::Plain,
       {entity(), s16("position"), u8("axis"), u8("speed")}),
    op(0x73, "entity_move_to_axis_with_camera", FlowRole::Plain,
       {entity(), s16("position"), u8("axis"), u8("speed")}),
    op(0x74, "spawn_item", FlowRole::Plain, {u8("item"), s16("x"), s16("z")}),
    op(0x75, "spawn_chest", FlowRole::Plain,
       {u8("item"), s16("x"), s16("y"), s16("z"), s16("w"), trigger()}),
    op(0x76, "spawn_boulder", FlowRole::Plain, {u8("unknown01")}),
    op(0x77, "move_boulder", FlowRole::Plain, {u8("unknown01"), s16("x"), s16("z")}),
    op(0x78, "despawn_boulder", FlowRole::Plain, {u8("unknown01")}),
    op(0x79, "unload_digimon", FlowRole::Plain, {u8("model")}),
    op(0x7A, "copy_pstat", FlowRole::Plain, {pad(), pstat("source"), pstat("destination")}),
    op(0x7B, "section_on_exit", FlowRole::Plain, {u8("section")}),
    op(0x7C, "set_rect_impassable", FlowRole::Plain,
       {u8("unknown01"), s16("x"), s16("z"), u8("width"), u8("height")}),
    op(0x7D, "spawn_sprite_at_location", FlowRole::Plain,
       {u8("sprite"), s16("x"), s16("y"), s16("z"), s16("w")}),
    op(0x7E, "spawn_sprite_at_entity", FlowRole::Plain, {entity(), u8("node"), u8("sprite")}),
    op(0xFB, "set_script", FlowRole::LoadScript, {pad(), u16("script"), u16("map")}),
    op(0xFC, "unused_fc", FlowRole::Plain, {}),
    op(0xFD, "unused_fd", FlowRole::Plain, {}),
    op(0xFE, "end_section", FlowRole::EndSection, {}),
    op(0xFF, "end_script", FlowRole::EndScript, {}),
};

constexpr std::uint8_t SetSelection = vocabulary::SetSelectionOpcode;
constexpr std::uint8_t SelectionEndMarker = 0x12;
constexpr std::uint8_t Switch = 0x18;
constexpr std::uint8_t If = 0x19;
constexpr std::uint8_t ShowTextbox = vocabulary::ShowTextboxOpcode;
constexpr std::uint8_t Data = 0x6B;
constexpr std::uint8_t EndScript = 0xFF;
// DATA blocks end with 0xFF. Their readers (dget.c: isTournamentEnabled,
// tournamentCheckEligible...) stop at the first byte >= 0xFE, and a trigger list that starts with
// 0xFE stands for every trigger: "FE FF".
constexpr std::uint8_t DataEnd = 0xFF;

// Whether the next instruction can run after one with this role.
constexpr bool fallsThrough(FlowRole role) {
  switch (role) {
  case FlowRole::Jump:
  case FlowRole::Return:
  case FlowRole::FileJump:
  case FlowRole::Switch:
  case FlowRole::Data:
  case FlowRole::EndSection:
  case FlowRole::EndScript:
    return false;
  case FlowRole::MapChange:
    // Nothing after a map change runs, but the sweep counted the code after it as reachable before
    // the role existed, and that count is in the listing (unreachable_instructions), which must not
    // change.
  default:
    return true;
  }
}

const OpcodeInfo* findOpcode(std::uint8_t code) {
  const auto found = std::ranges::find(Opcodes, code, &OpcodeInfo::code);
  return found == Opcodes.end() ? nullptr : &*found;
}

std::size_t fieldSize(ArgumentKind kind) {
  switch (kind) {
  case U16:
  case S16:
  case Trigger:
  case Label:
    return 2;
  case S32:
    return 4;
  case Pad:
  case U8:
  case Pstat:
  case Entity:
    return 1;
  }
  return 1;
}

// Why an instruction did not decode; the sweep stops there.
struct DecodeError {
  std::string message;
};

// Reads the fields of an instruction one after another.
class Reader {
public:
  Reader(std::span<const std::byte> script, Instruction& instruction)
      : bytes_(script), instruction_(&instruction), at_(instruction.offset) {}

  [[nodiscard]] std::uint32_t at() const { return at_; }

  // Reads a field; a pad that is not 0 is kept, other fields become arguments.
  std::optional<std::int32_t> field(ArgumentKind kind, std::string_view name) {
    const std::size_t size = fieldSize(kind);
    if (!bytes_.contains(at_, size)) {
      return std::nullopt;
    }
    std::int32_t value = 0;
    switch (size) {
    case 1:
      value = bytes_.u8(at_).value_or(0);
      break;
    case 2: {
      const std::uint16_t half = bytes_.u16(at_).value_or(0);
      value = kind == S16 ? static_cast<std::int16_t>(half) : half;
      break;
    }
    default:
      value = bytes_.s32(at_).value_or(0);
      break;
    }
    if (kind == Pad) {
      if (value != 0) {
        instruction_->pads.push_back(
            PadByte{.offset = static_cast<std::uint8_t>(at_ - instruction_->offset),
                    .value = static_cast<std::uint8_t>(value)});
      }
    } else if (!name.empty()) {
      instruction_->arguments.push_back(Argument{.name = name, .kind = kind, .value = value});
    }
    at_ += static_cast<std::uint32_t>(size);
    return value;
  }

  [[nodiscard]] std::optional<std::uint8_t> peek() const { return bytes_.u8(at_); }

  void skip(std::uint32_t count) { at_ += count; }

private:
  ByteReader bytes_;
  Instruction* instruction_;
  std::uint32_t at_;
};

constexpr std::array<std::string_view, 6> CompareNames = {"eq", "ne", "ge", "le", "gt", "lt"};

// The test of an IF entry by its code: bits 3-5 pick the kind and, for the extended kind 0x20, the
// low bits the test.
std::optional<ConditionTest> conditionTestOf(std::uint8_t code) {
  constexpr std::array<ConditionTest, 4> Kinds = {ConditionTest::Trigger, ConditionTest::Pstat,
                                                  ConditionTest::GotoIfTrue,
                                                  ConditionTest::GotoIfFalse};
  constexpr std::array<ConditionTest, 6> Extended = {
      ConditionTest::Stat,      ConditionTest::Card,      ConditionTest::Move,
      ConditionTest::Condition, ConditionTest::ItemCount, ConditionTest::Money};
  constexpr unsigned KindShift = 3;
  constexpr unsigned KindMask = 0x07U;
  constexpr unsigned ExtendedKind = 4;
  constexpr unsigned LowMask = 0x07U;
  const unsigned kind = (code >> KindShift) & KindMask;
  if (kind < Kinds.size()) {
    return Kinds.at(kind);
  }
  const unsigned low = code & LowMask;
  if (kind == ExtendedKind && low < Extended.size()) {
    return Extended.at(low);
  }
  return std::nullopt;
}

// The fields of an IF entry after its code and pad (script_interp.c: scriptIfInstruction,
// script_value.c: scriptCompare*).
std::vector<Field> conditionPayload(ConditionTest test) {
  switch (test) {
  case ConditionTest::Trigger:
    return {trigger()};
  case ConditionTest::Pstat:
    return {pstat(), u8("value")};
  case ConditionTest::GotoIfTrue:
  case ConditionTest::GotoIfFalse:
    return {label()};
  case ConditionTest::Stat:
    return {u8("stat"), u8("compare"), u16("value")};
  case ConditionTest::Card:
    return {u8("card"), u8("compare"), u16("value")};
  case ConditionTest::Move:
    return {u8("move"), u8("negate")};
  case ConditionTest::Condition:
    return {u8("mask"), u8("negate")};
  case ConditionTest::ItemCount:
    return {u8("item"), u8("compare"), u16("value")};
  case ConditionTest::Money:
    return {pad(), u8("compare"), s32("value")};
  }
  return {};
}

// Reads the entries of an IF after its pad(), up to the 0x19 entry that ends it.
std::optional<DecodeError> decodeIf(Reader& reader, Instruction& instruction) {
  while (true) {
    const auto code = reader.peek();
    if (!code) {
      return DecodeError{"the IF runs past the end of the script"};
    }
    if (*code == If) {
      reader.skip(1);
      if (!reader.field(Pad, "")) {
        return DecodeError{"the IF runs past the end of the script"};
      }
      return std::nullopt;
    }
    Condition condition;
    condition.code = *code;
    if ((*code & 0x80U) != 0) {
      condition.combine = Combine::And;
    } else if ((*code & 0x40U) != 0) {
      condition.combine = Combine::Or;
    }
    const unsigned low = *code & 0x07U;
    const auto test = conditionTestOf(*code);
    if (!test) {
      return DecodeError{std::format("IF test {:#04x} is not one the interpreter has", *code)};
    }
    condition.test = *test;
    const std::vector<Field> payload = conditionPayload(*test);
    if (condition.test == ConditionTest::Pstat && low >= CompareNames.size()) {
      return DecodeError{
          std::format("IF test {:#04x} compares with operator {}, which has no case", *code, low)};
    }
    // The arguments of an entry go to the condition, not to the instruction.
    const std::size_t before = instruction.arguments.size();
    reader.skip(1);
    bool complete = reader.field(Pad, "").has_value();
    for (const Field& field : payload) {
      complete = complete && reader.field(field.kind, field.name).has_value();
    }
    if (!complete) {
      return DecodeError{"the IF runs past the end of the script"};
    }
    const auto first = instruction.arguments.begin() + static_cast<std::ptrdiff_t>(before);
    const auto badCompare =
        std::find_if(first, instruction.arguments.end(), [](const Argument& argument) {
          return argument.name == "compare" && (argument.value & 0x07) >= std::ssize(CompareNames);
        });
    if (badCompare != instruction.arguments.end()) {
      return DecodeError{std::format("IF test {:#04x} compares with operator {}, which has no case",
                                     *code, badCompare->value & 0x07)};
    }
    condition.arguments.assign(first, instruction.arguments.end());
    instruction.arguments.erase(first, instruction.arguments.end());
    instruction.conditions.push_back(std::move(condition));
  }
}

std::optional<DecodeError> decodeText(std::span<const std::byte> script, Reader& reader,
                                      Instruction& instruction) {
  const auto text = text::decodeScriptText(script.subspan(reader.at()));
  if (!text) {
    return DecodeError{std::format("text: {}", text.error().message)};
  }
  instruction.text = text->text;
  reader.skip(static_cast<std::uint32_t>(text->size));
  return std::nullopt;
}

// Decodes the instruction at an offset into `instruction`, its size included.
std::optional<DecodeError> decodeInstruction(std::span<const std::byte> script,
                                             Instruction& instruction) {
  const OpcodeInfo* info = findOpcode(instruction.opcode);
  if (info == nullptr) {
    return DecodeError{
        std::format("opcode {:#04x} is not one the interpreter dispatches", instruction.opcode)};
  }
  Reader reader{script, instruction};
  reader.skip(1);
  for (const Field& field : info->layout()) {
    if (!reader.field(field.kind, field.name)) {
      return DecodeError{
          std::format("the operands of {} run past the end of the script", info->name)};
    }
  }
  const auto truncated = [&] {
    return DecodeError{
        std::format("the operands of {} run past the end of the script", info->name)};
  };
  switch (instruction.opcode) {
  case SetSelection: {
    // The options, then a SHOW_TEXTBOX word that scriptShowSelection counts as one more table
    // entry.
    const auto count = static_cast<std::size_t>(instruction.arguments.back().value);
    instruction.arguments.pop_back();
    for (std::size_t i = 0; i < count; ++i) {
      const auto target = reader.field(U16, "");
      if (!target) {
        return truncated();
      }
      instruction.targets.push_back(static_cast<std::uint16_t>(*target));
    }
    if (reader.peek() != ShowTextbox) {
      return DecodeError{"SET_SELECTION is not followed by its SHOW_TEXTBOX word"};
    }
    reader.skip(1);
    if (!reader.field(Pad, "")) {
      return truncated();
    }
    if (auto error = decodeText(script, reader, instruction)) {
      return error;
    }
    ByteReader bytes{script};
    if (bytes.u8(reader.at()) == SelectionEndMarker && bytes.u8(reader.at() + 1) == 0) {
      instruction.selectionEndMarker = true;
      reader.skip(2);
    }
    break;
  }
  case Switch: {
    const auto count = static_cast<std::size_t>(instruction.arguments.back().value);
    instruction.arguments.pop_back();
    for (std::size_t i = 0; i < count; ++i) {
      const auto target = reader.field(U16, "");
      if (!target) {
        return truncated();
      }
      instruction.targets.push_back(static_cast<std::uint16_t>(*target));
    }
    break;
  }
  case If:
    if (auto error = decodeIf(reader, instruction)) {
      return error;
    }
    break;
  case ShowTextbox:
    if (auto error = decodeText(script, reader, instruction)) {
      return error;
    }
    break;
  case Data:
    while (true) {
      const auto byte = reader.peek();
      if (!byte) {
        return DecodeError{"DATA runs past the end of the script"};
      }
      instruction.data.push_back(*byte);
      reader.skip(1);
      if (*byte == DataEnd) {
        break;
      }
    }
    break;
  default:
    break;
  }
  instruction.size = reader.at() - instruction.offset;
  return std::nullopt;
}

// Every offset an instruction can jump to.
std::vector<std::uint16_t> targetsOf(const Instruction& instruction) {
  std::vector<std::uint16_t> targets = instruction.targets;
  const auto addLabels = [&targets](const std::vector<Argument>& arguments) {
    for (const Argument& argument : arguments) {
      if (argument.kind == Label) {
        targets.push_back(static_cast<std::uint16_t>(argument.value));
      }
    }
  };
  addLabels(instruction.arguments);
  for (const Condition& condition : instruction.conditions) {
    addLabels(condition.arguments);
  }
  return targets;
}

// Counts the instructions that no flow from a section entry reaches.
std::size_t countUnreachable(const Disassembly& disassembly) {
  const auto& instructions = disassembly.instructions;
  const auto indexOf = [&instructions](std::uint32_t offset) -> std::optional<std::size_t> {
    const auto found = std::ranges::lower_bound(instructions, offset, {}, &Instruction::offset);
    if (found == instructions.end() || found->offset != offset) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(found - instructions.begin());
  };
  std::vector<bool> reached(instructions.size(), false);
  std::vector<std::uint32_t> work;
  work.reserve(disassembly.header.sections.size());
  for (const ScriptSection& section : disassembly.header.sections) {
    work.push_back(section.offset);
  }
  while (!work.empty()) {
    auto index = indexOf(work.back());
    work.pop_back();
    while (index && !reached[*index]) {
      reached[*index] = true;
      const Instruction& instruction = instructions[*index];
      const auto targets = targetsOf(instruction);
      work.insert(work.end(), targets.begin(), targets.end());
      if (!fallsThrough(findOpcode(instruction.opcode)->flow)) {
        break;
      }
      index = *index + 1 < instructions.size() ? std::optional{*index + 1} : std::nullopt;
    }
  }
  return static_cast<std::size_t>(std::ranges::count(reached, false));
}

std::vector<OperandField> namedFields(std::span<const Field> fields) {
  std::vector<OperandField> named;
  for (const Field& field : fields) {
    if (field.kind != Pad) {
      named.push_back({.name = field.name, .kind = field.kind});
    }
  }
  return named;
}

} // namespace

std::string_view opcodeName(std::uint8_t opcode) {
  const OpcodeInfo* info = findOpcode(opcode);
  return info == nullptr ? std::string_view{} : info->name;
}

std::optional<std::uint8_t> opcodeNamed(std::string_view name) {
  const auto found = std::ranges::find(Opcodes, name, &OpcodeInfo::name);
  return found == Opcodes.end() ? std::nullopt : std::optional{found->code};
}

std::vector<OperandField> opcodeOperands(std::uint8_t opcode) {
  const OpcodeInfo* info = findOpcode(opcode);
  if (info == nullptr) {
    return {};
  }
  std::span<const Field> layout = info->layout();
  // The last field of SET_SELECTION and SWITCH is how many targets follow, which the targets imply.
  if (opcode == SetSelection || opcode == Switch) {
    layout = layout.first(layout.size() - 1);
  }
  return namedFields(layout);
}

std::vector<OperandField> conditionOperands(ConditionTest test) {
  return namedFields(conditionPayload(test));
}

FlowRole opcodeFlowRole(std::uint8_t opcode) {
  const OpcodeInfo* info = findOpcode(opcode);
  return info == nullptr ? FlowRole::Plain : info->flow;
}

core::Result<Disassembly> disassembleScript(std::span<const std::byte> script) {
  auto header = readScriptHeader(script);
  if (!header) {
    return std::unexpected{header.error()};
  }
  Disassembly disassembly;
  disassembly.header = std::move(*header);
  std::set<std::uint16_t> labels;
  std::uint32_t farthest = 0;
  for (const ScriptSection& section : disassembly.header.sections) {
    labels.insert(section.offset);
    farthest = std::max<std::uint32_t>(farthest, section.offset);
  }
  const ByteReader bytes{script};
  std::uint32_t at = disassembly.header.codeStart;
  while (true) {
    const auto opcode = bytes.u8(at);
    if (!opcode) {
      disassembly.unknowns.push_back(std::format(
          "{:#06x}: the code reaches the end of the script without an END_SCRIPT past its targets",
          at));
      break;
    }
    Instruction instruction;
    instruction.offset = at;
    instruction.opcode = *opcode;
    if (const auto error = decodeInstruction(script, instruction)) {
      disassembly.unknowns.push_back(std::format("{:#06x}: {}; {} bytes from here are not decoded",
                                                 at, error->message, script.size() - at));
      break;
    }
    at += instruction.size;
    bool desync = false;
    if (at % 2 != 0) {
      // The pad after a 1-byte instruction. DATA is read a byte at a time, so the arena scripts
      // pack their blocks one after another at any offset; anything else at an odd offset would
      // break the aligned reads.
      const auto next = bytes.u8(at);
      if (next == 0) {
        ++instruction.size;
        ++at;
      } else {
        desync = next.has_value() && *next != Data;
      }
    }
    for (const std::uint16_t target : targetsOf(instruction)) {
      labels.insert(target);
      farthest = std::max<std::uint32_t>(farthest, target);
    }
    const bool end = instruction.opcode == EndScript && at > farthest;
    disassembly.instructions.push_back(std::move(instruction));
    if (desync) {
      disassembly.unknowns.push_back(std::format(
          "{:#06x}: an instruction other than DATA starts at an odd offset; {} bytes from here are "
          "not decoded",
          at, script.size() - at));
      break;
    }
    if (end) {
      break;
    }
  }
  disassembly.codeEnd = at;
  disassembly.labels.assign(labels.begin(), labels.end());
  // Labels past codeEnd are left to the unknown that stopped the sweep.
  for (const std::uint16_t offset : disassembly.labels) {
    if (offset >= disassembly.codeEnd) {
      continue;
    }
    const bool onInstruction = std::ranges::binary_search(
        disassembly.instructions, std::uint32_t{offset}, {}, &Instruction::offset);
    if (!onInstruction) {
      disassembly.unknowns.push_back(
          std::format("{:#06x}: a section or target that is not an instruction", offset));
    }
  }
  disassembly.unreachable = countUnreachable(disassembly);
  return disassembly;
}

} // namespace dwe::formats::script
