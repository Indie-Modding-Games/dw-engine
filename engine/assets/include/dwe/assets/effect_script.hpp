#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

// A technique's effect scripts as dwx extracts them in effect/<technique>/effect.yaml and
// script.yaml (docs/formats/efectos.md): what the battle runs while the technique plays
// (battle_effect.c: BTL_runEFEScript). Instructions keep the original's offsets and opcodes, so the
// interpreter can follow the scripts as the original does.

// The opcodes of the effect scripts (battle_effect.c: BTL_jtbl_80073604).
enum class EffectOpcode : std::uint8_t {
  LoadImmediate = 0x00, // register = value
  LoadVariable = 0x01,  // register = variable
  LoadRandom = 0x02,    // register = rand()
  LoadIndexed = 0x03,   // register = table[index variable]
  ApplyOperator = 0x04, // variable <operator>= register
  Stop = 0x05,
  Stop6 = 0x06,
  BranchIf = 0x07, // goes to the target when (variable <comparison> register) holds
  Stop8 = 0x08,
  Jump = 0x09,
  PushImmediate = 0x0A,
  PushVariable = 0x0B,
  PushAddress = 0x0C,
  Call = 0x0D,
  SubOp = 0x0E, // takes its arguments from the stack
  Return = 0x0F,
  PopToVariable = 0x10,
  Spawn = 0x11, // starts a sub-effect on the first free instance of a table
};

enum class EffectWidth : std::uint8_t { Int8 = 1, Int16 = 2, Int32 = 4 };

enum class EffectOperator : std::uint8_t {
  Set,
  Add,
  Subtract,
  Multiply,
  Divide,
  Modulo,
  ShiftLeft,
  ShiftRight,
};

enum class EffectComparison : std::uint8_t {
  Equal,
  NotEqual,
  Less,
  LessOrEqual,
  Greater,
  GreaterOrEqual,
};

// A variable: an offset from the slot's base or from the running sub-effect's instance.
struct EffectVariable {
  bool base = false;
  std::int16_t offset = 0;

  bool operator==(const EffectVariable&) const = default;
};

struct EffectInstruction {
  std::uint32_t offset = 0; // from the slot's base
  EffectOpcode opcode = EffectOpcode::Stop;
  EffectWidth width = EffectWidth::Int32;      // of the variable, or of the table's elements
  EffectWidth indexWidth = EffectWidth::Int32; // load_indexed: of the index variable
  EffectOperator op = EffectOperator::Set;
  EffectComparison comparison = EffectComparison::Equal;
  EffectVariable variable; // the index for load_indexed
  std::int16_t value = 0;  // load_immediate, push_immediate
  std::int32_t target = 0; // from the base: the code, load_indexed's table, spawn's instances
  std::int16_t count = 0;  // spawn: instances in the table
  std::int16_t stride = 0; // spawn: bytes from one instance to the next
  std::uint8_t ordinal = 0;
  std::string sub;           // sub: the sub-op's name
  std::uint8_t argCount = 0; // sub: the values it pops
};

// An entry of a bone-offset list (battle_effect.c: EfeTechBoneOffset): where on a species the
// effect starts, as a node of its model and an offset in that node's space. The list ends at the
// first entry with a negative type, which the YAML leaves out.
struct EffectBoneOffset {
  std::string species; // the species table's id of the original type (getOriginalType)
  std::int16_t bone = 0;
  std::array<std::int16_t, 3> offset{};

  bool operator==(const EffectBoneOffset&) const = default;
};

struct EffectScript {
  std::string move;                 // the technique, as the move table names it
  std::uint16_t subEffectCount = 0; // the sub-effects that can run at once
  // The objects of the slot's model: one UV animation each (uv_anim_count; numObjects), 0 without
  // a model.
  std::uint16_t modelObjects = 0;
  std::uint32_t startScript = 0; // offsets from the slot's base
  std::uint32_t initScript = 0;
  std::vector<EffectInstruction> instructions; // in offset order
  // The slot's bytes from its base up to the start script, fill (0xCD) where the YAML gives no
  // value: the variables, tables, instance tables, the numbers, vectors and colours the sub-ops
  // point at and the UV keyframes get their values, and each bone-offset entry its bone and offset
  // (its type is a name: boneOffsets); the CLUT colours, parallax sprites, tile maps and ribbons
  // stay fill.
  std::vector<std::byte> data;
  // The bone-offset lists by their offset from the base.
  std::map<std::uint32_t, std::vector<EffectBoneOffset>> boneOffsets;

  // The index of the instruction at an offset, or nothing.
  [[nodiscard]] std::optional<std::size_t> instructionAt(std::uint32_t offset) const;
};

// Reads an effect.yaml and its script.yaml. Fails on an unknown op, operator or comparison, a
// variable or label it cannot read, and data outside the data area.
core::Result<EffectScript> readEffectScript(const core::YamlNode& effect,
                                            const core::YamlNode& script);

} // namespace dwe::assets
