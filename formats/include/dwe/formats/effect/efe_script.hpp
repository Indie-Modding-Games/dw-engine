#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/effect/efe_file.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// The effect scripts: fixed-length instructions run by battle_effect.c: BTL_runEFEScript through BTL_jtbl_80073604.
// An instruction starts with a halfword whose low byte is the opcode and whose high byte is a variant: a sub-op, a
// width, an operator or a comparison. Its operands are the halfwords that follow.
namespace dwe::formats::effect {

enum class EfeOpcode : std::uint8_t {
    LoadImmediate = 0x00, // register = value
    LoadVariable = 0x01,  // register = variable
    LoadRandom = 0x02,    // register = rand()
    LoadIndexed = 0x03,   // register = table[index variable]
    ApplyOperator = 0x04, // variable <operator>= register
    Stop = 0x05,          // ends the script; 0x06 and 0x08 run the same handler
    Stop6 = 0x06,
    BranchIf = 0x07, // goes to the target when (variable <comparison> register) holds
    Stop8 = 0x08,
    Jump = 0x09,
    PushImmediate = 0x0A,
    PushVariable = 0x0B,
    PushAddress = 0x0C, // pushes the address of a variable or of data
    Call = 0x0D,
    SubOp = 0x0E, // runs one of the 97 sub-ops, which take their arguments from the stack
    Return = 0x0F,
    PopToVariable = 0x10,
    Spawn = 0x11, // starts a sub-effect on the first free instance of a table
};

inline constexpr std::size_t EfeOpcodeCount = 0x12;
inline constexpr std::size_t EfeSubOpCount = 0x61;

// The instruction's length in bytes, or nothing for a byte that is not an opcode.
std::optional<std::size_t> efeInstructionSize(std::uint8_t opcode);

// The name scripts and documents use for an opcode: "push_address".
std::string_view efeOpcodeName(EfeOpcode opcode);

// The width of a variable access, from a variant nibble: 4 and 2 are int32 and int16, anything else int8.
enum class EfeWidth : std::uint8_t { Int8 = 1, Int16 = 2, Int32 = 4 };

// BTL_EFE_VARIABLE_OPERATORS: what opcode 0x04 does to its variable with the register.
enum class EfeOperator : std::uint8_t { Set, Add, Subtract, Multiply, Divide, Modulo, ShiftLeft, ShiftRight };

// BTL_EFE_COMPARISONS: variable against register.
enum class EfeComparison : std::uint8_t { Equal, NotEqual, Less, LessOrEqual, Greater, GreaterOrEqual };

std::string_view efeOperatorName(EfeOperator op);
std::string_view efeComparisonName(EfeComparison comparison);

// Where a variable lives: a flag of 0 counts its offset from the running sub-effect's EfeInstance, any other flag
// (0xDAAD on the disc) from the slot's base.
enum class EfeSpace : std::uint8_t { Instance, Base };

inline constexpr std::uint16_t EfeBaseFlag = 0xDAAD;
// What fills the operand halfwords an instruction does not use.
inline constexpr std::uint16_t EfeFillHalfword = 0xCDCD;

struct EfeVariable {
    EfeSpace space = EfeSpace::Instance;
    std::int16_t offset = 0;
    std::uint16_t flag = 0; // as stored: 0, or the base flag

    bool operator==(const EfeVariable&) const = default;
};

// What the authoring tool stored in a halfword or variant byte that the handler never reads.
enum class EfeIgnoredKind : std::uint8_t {
    Fill,      // 0xCDCD, or 0xCD for a variant byte
    BaseFlag,  // 0xDAAD: the flag of an address the handler always takes from the base
    SignOfLow, // the high half of a 32-bit literal: 0 or 0xFFFF, the sign of the half the handler reads
    Zero,      // the high half of a 32-bit stride
};

// An operand halfword or variant byte the handler never reads. index 0 is the variant byte, 1 the first operand.
struct EfeIgnoredOperand {
    std::size_t index = 0;
    std::string_view name;
    EfeIgnoredKind kind = EfeIgnoredKind::Fill;
};

// The ignored operands of an opcode.
std::span<const EfeIgnoredOperand> efeIgnoredOperands(EfeOpcode opcode);

// One instruction, with every operand decoded. Which fields mean something depends on the opcode; the rest keep
// their defaults. `operands` keeps every halfword after the first as stored, read or not.
struct EfeInstruction {
    std::uint32_t offset = 0; // from the slot's base
    EfeOpcode opcode = EfeOpcode::Stop;
    std::uint16_t word = 0;
    std::vector<std::uint16_t> operands;
    EfeWidth width = EfeWidth::Int32;      // of the variable, or of the table's elements (0x03)
    EfeWidth indexWidth = EfeWidth::Int32; // 0x03: of the index variable
    EfeOperator op = EfeOperator::Set;
    EfeComparison comparison = EfeComparison::Equal;
    EfeVariable variable;    // 0x01, 0x04, 0x07, 0x0B, 0x0C, 0x10; the index for 0x03
    std::int16_t value = 0;  // 0x00, 0x0A: the immediate, as the handler reads it (one halfword)
    std::int32_t target = 0; // from the base: 0x07, 0x09, 0x0D the code; 0x03 the table; 0x11 the instance table
    std::int16_t count = 0;  // 0x11: instances in the table
    std::int16_t stride = 0; // 0x11: bytes from one instance to the next
    std::uint8_t subOp = 0;
    std::uint8_t ordinal = 0; // 0x11: the variant byte, which numbers the slot's spawns from 0

    [[nodiscard]] std::uint8_t variant() const { return static_cast<std::uint8_t>(word >> 8U); }
    // The stored value of an ignored operand (EfeIgnoredOperand::index) and whether it is the usual one.
    [[nodiscard]] std::uint16_t ignoredValue(std::size_t index) const;
    [[nodiscard]] bool isUsualIgnored(const EfeIgnoredOperand& ignored) const;

    bool operator==(const EfeInstruction&) const = default;
};

// What a sub-op does with one of its stack arguments, which says how much data an address argument points at.
enum class EfeArgKind : std::uint8_t {
    Value,           // a number
    Unused,          // popped and never used
    Code,            // the address of a script
    Int32,           // the address of an int32 the handler reads or writes
    Vector,          // the address of 3 int32 x, y, z (EfeVector, or a VECTOR whose pad is not read)
    Color,           // the address of 3 int32 r, g, b (EfeColor)
    BoneOffsets,     // EfeTechBoneOffset entries of 10 bytes, ending with an int16 type < 0
    UvKeyframes,     // int32 count, then count EfeUvKeyframe {int16 frames; int8 du, dv}; or -1 for none
    ParentField,     // the address of a variable as wide as the size argument says: 12, 8, 4, 2 or 1 bytes
    Int32Array,      // int32[the count argument]
    ClutColors,      // the count argument's {uint16 r, g, b, stp} colours
    ClutRow,         // 16 raw colours from the colour index argument on
    TileMap,         // 11 x 9 bytes of tile ids
    TileOffsets,     // an int16 per tile id the tile map argument uses
    ParallaxSprites, // EfeParallaxSprite entries of 10 bytes, ending with an int16 size < 0
    RibbonPoints,    // 10 SVECTOR
    QuadCorner,      // a number, or the address of {int32 x, y} when the quad's semi-transparency is negative
};

// One stack argument of a sub-op.
struct EfeArg {
    std::string_view name;
    EfeArgKind kind = EfeArgKind::Value;
    // For ParentField, Int32Array, ClutColors, ClutRow, TileOffsets and QuadCorner: the argument that sizes or
    // selects the data (the size, the count, the colour index, the tile map, the semi-transparency).
    std::uint8_t related = 0;
};

// A sub-op of opcode 0x0E (BTL_EFE_SUB_OPCODES): its name, its handler in the decomp and its arguments in the order
// a script pushes them. The handler pops them in the reverse order. No sub-op leaves a value on the stack.
struct EfeSubOpSpec {
    std::string_view name;
    std::string_view handler;
    std::span<const EfeArg> args;
};

// Returns the sub-op with this code, which must be below EfeSubOpCount.
const EfeSubOpSpec& efeSubOp(std::uint8_t code);

// Decodes the instruction at an offset of the payload (from the slot's base). Fails on a byte that is not an opcode,
// a sub-op past the table, an operator or comparison past theirs, and an instruction that runs past `end`.
core::Result<EfeInstruction> decodeEfeInstruction(std::span<const std::byte> payload, std::uint32_t offset,
                                                  std::uint32_t end);

// The two scripts of a slot, walked instruction by instruction from their start to their end. The original never
// walks them like that, it follows the instructions as they run, but every instruction has a fixed length, so a linear
// walk finds them all.
struct EfeScripts {
    std::vector<EfeInstruction> instructions; // the start script's, then the init script's, in offset order
    // Offsets of a fill halfword (0xCDCD) after a script's last return: alignment, never run.
    std::vector<std::uint32_t> padding;
};

// Decodes [startScript, initScript) and [initScript, uvAnims). Each must end exactly at its end, or with one fill
// halfword after a return.
core::Result<EfeScripts> decodeEfeScripts(std::span<const std::byte> payload, const EfeHeader& header);

} // namespace dwe::formats::effect
