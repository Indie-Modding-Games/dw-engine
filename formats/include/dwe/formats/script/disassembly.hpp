#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/script/scn_file.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The instructions of an event script, decoded with the opcode set of include/dw/script.h and the operand reads of
// script_interp.c: tickScript and its scriptInstruction* handlers.
//
// Operands are little-endian; 16- and 32-bit ones are read with aligned loads, so the encodings carry pad bytes
// that the interpreter skips. Instructions start at even offsets (the 1-byte ones are followed by a 0x00 pad), except
// DATA blocks, which are read a byte at a time and packed back to back.
// Jump targets are byte offsets from the start of the script.
namespace dwe::formats::script {

enum class ArgumentKind : std::uint8_t {
    Pad,     // a byte the interpreter skips
    U8,      // an unsigned byte
    U16,     // an unsigned 16-bit value
    S16,     // a signed 16-bit value
    S32,     // a signed 32-bit value, read as two halves (pollNextInt)
    Pstat,   // the index of a script variable (ScriptState.pstats)
    Trigger, // the number of a flag bit (ScriptState.triggers)
    Entity,  // an NPC's script id, 0xFD the tamer or 0xFC the partner (scriptIdToEntityId)
    Label,   // a jump target
};

struct Argument {
    std::string_view name;
    ArgumentKind kind = ArgumentKind::U8;
    std::int32_t value = 0;

    bool operator==(const Argument&) const = default;
};

// What an IF entry tests (script_interp.c: scriptIfInstruction, script_value.c: scriptCompare*).
enum class ConditionTest : std::uint8_t {
    Trigger,     // a trigger is set (low bits 0) or clear
    Pstat,       // a variable against a value
    GotoIfTrue,  // jumps if the result so far is true
    GotoIfFalse, // jumps if it is false
    Stat,        // a partner stat (SCRIPT_STAT_*); stat 9 compares signed
    Card,        // how many of a card
    Move,        // whether the partner knows a move
    Condition,   // whether the partner has a condition
    ItemCount,   // how many of an item
    Money,
};

// How an IF entry combines with the result so far: bit 7 is AND, else bit 6 is OR, else it replaces it.
enum class Combine : std::uint8_t { Replace, And, Or };

struct Condition {
    std::uint8_t code = 0; // the entry's first byte, which holds the test, the combination and the comparison
    ConditionTest test = ConditionTest::Trigger;
    Combine combine = Combine::Replace;
    std::vector<Argument> arguments;

    bool operator==(const Condition&) const = default;
};

// A byte the interpreter skips that is not 0, by its offset inside the instruction.
struct PadByte {
    std::uint8_t offset = 0;
    std::uint8_t value = 0;

    bool operator==(const PadByte&) const = default;
};

struct Instruction {
    std::uint32_t offset = 0;
    std::uint32_t size = 0; // every byte, pads included
    std::uint8_t opcode = 0;
    std::vector<Argument> arguments;
    std::vector<std::uint16_t> targets; // SWITCH cases and SET_SELECTION options, in order
    std::vector<Condition> conditions;  // IF
    std::optional<std::string> text;    // SHOW_TEXTBOX and SET_SELECTION, decoded by text::decodeScriptText
    std::vector<std::uint8_t> data;     // DATA: the bytes after the pad, up to and with the closing 0xFF
    bool selectionEndMarker = false;    // SET_SELECTION followed by 0x12 0x00, which a cancel skips
    std::vector<PadByte> pads;

    bool operator==(const Instruction&) const = default;
};

struct Disassembly {
    ScriptHeader header;
    std::vector<Instruction> instructions; // in offset order, from codeStart
    std::uint32_t codeEnd = 0;             // the offset after the last decoded byte
    // Sorted and unique: every section start and every target. A label not on an instruction is an unknown.
    std::vector<std::uint16_t> labels;
    std::size_t unreachable = 0; // instructions no flow from a section entry reaches
    // What did not decode: an unknown opcode or IF test, operands past the end, text that does not end, an odd
    // instruction start, a target or section that is not an instruction. Decoding stops at the first of the
    // first four, so codeEnd marks where.
    std::vector<std::string> unknowns;
};

// What an instruction does to the flow of a script.
enum class FlowRole : std::uint8_t {
    Plain,      // runs and goes on to the next instruction
    Jump,       // jump_to: goes to its target
    Call,       // jump_and_link: runs the subroutine at its target, then goes on
    Return,     // jump_return: back to the instruction after the jump_and_link
    FileCall,   // jump_to_file_and_link: runs a section of another script, then goes on
    FileJump,   // jump_to_file: leaves this script for a section of another one
    LoadScript, // set_script: loads another script and map, then goes on
    MapChange,  // warp_to, teleport: starts a map change; nothing after it runs (scriptInstruction46to58)
    If,         // goes to the target of a jump entry whose test holds, else on
    Switch,     // goes to the case a variable selects
    Selection,  // goes to the option the player picks
    EndSection, // ends the script
    EndScript,  // ends the script
    Data,       // a table that tournaments read; never runs
};

// The name of an opcode in lowercase ("show_textbox"), or empty if the interpreter does not dispatch it.
std::string_view opcodeName(std::uint8_t opcode);
// The opcode with a name, if the interpreter dispatches one by that name.
std::optional<std::uint8_t> opcodeNamed(std::string_view name);
// An operand as the listing names it.
struct OperandField {
    std::string_view name;
    ArgumentKind kind = ArgumentKind::U8;
};
// The operands of an opcode in their order, without pads; empty for one the interpreter does not dispatch. The
// variable parts (IF entries, SWITCH cases, SET_SELECTION options, text, DATA) are not in it.
std::vector<OperandField> opcodeOperands(std::uint8_t opcode);
// The operands of an IF entry with this test, in their order, without pads.
std::vector<OperandField> conditionOperands(ConditionTest test);
// How an opcode moves the flow; Plain for one the interpreter does not dispatch.
FlowRole opcodeFlowRole(std::uint8_t opcode);

// Decodes a script: a linear sweep from codeStart that stops at the first END_SCRIPT (0xFF) that lies past every
// section start and target seen so far. The arena scripts keep DATA blocks after their first END_SCRIPT, which jump
// tables reach, so the sweep goes on through them. Bytes after codeEnd are not read: in DG.SCN they are the stale
// padding of the script's sectors. Fails only if the header does not read.
core::Result<Disassembly> disassembleScript(std::span<const std::byte> script);

} // namespace dwe::formats::script
