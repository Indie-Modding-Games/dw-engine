#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/script/disassembly.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A script listing as the tools work with it: each instruction with what it does to the flow and its operands, read
// back from the YAML that dwx writes and the map editor edits, or taken from a disassembly. It is the one decoded
// form of a script: the control-flow graph, the structuring, the map changes and the Lua converter read it. DATA
// bytes and pad bytes stay in the YAML.
namespace dwe::formats::script {

// A jump entry of an IF: it jumps when the result of the entries before it is true (goto_if_true) or false.
struct ConditionalJump {
    std::size_t entry = 0; // the jump entry's place in the IF's conditions
    bool whenTrue = true;
    std::uint32_t target = 0;

    bool operator==(const ConditionalJump&) const = default;
};

// A section of another script that an instruction runs.
struct ScriptReference {
    std::uint16_t script = 0;
    std::uint16_t section = 0;

    bool operator==(const ScriptReference&) const = default;
};

struct ListingInstruction {
    std::uint32_t offset = 0;
    std::string op;
    FlowRole role = FlowRole::Plain;
    std::vector<std::uint32_t> targets;            // Jump and Call: one; Switch: the cases; Selection: the options
    std::vector<ConditionalJump> conditionalJumps; // If, in entry order
    std::optional<ScriptReference> reference;      // FileCall and FileJump
    std::vector<Argument> arguments;               // the operands, named as the disassembler names them
    std::vector<Condition> conditions;             // If, every entry in order
    // SHOW_TEXTBOX and SET_SELECTION: the text as the listing writes it (text::decodeScriptText; a selection's
    // options one per line).
    std::optional<std::string> text;

    // The value of the operand with this name, if the instruction has one.
    [[nodiscard]] std::optional<std::int32_t> argument(std::string_view name) const;

    bool operator==(const ListingInstruction&) const = default;
};

struct ListingSection {
    std::uint16_t id = 0;
    std::uint32_t entry = 0; // the offset of its first instruction

    bool operator==(const ListingSection&) const = default;
};

struct ScriptListing {
    std::vector<ListingSection> sections;
    std::vector<ListingInstruction> instructions; // in offset order

    // The index of the instruction at an offset, if one starts there.
    [[nodiscard]] std::optional<std::size_t> indexAt(std::uint32_t offset) const;
    // The sections the game can run: the first entry of each id, in the table's order. A later entry with the same id
    // is never run, as the game takes the first that matches (script_interp.c: getScriptSection).
    [[nodiscard]] std::vector<ListingSection> runnableSections() const;

    bool operator==(const ScriptListing&) const = default;
};

// The flow role of an op name ("jump_to"); Plain for the ops that do not change the flow.
FlowRole flowRoleOf(std::string_view op);

// Reads a listing. Fails on a listing that is not one: a missing `at` or `op`, a missing or malformed operand of an op
// the interpreter has (the `text` of SHOW_TEXTBOX and SET_SELECTION included), an IF entry with an unknown test, a
// label that is not l_<hex>, instructions out of order, or a section or target that is not an instruction.
core::Result<ScriptListing> readScriptListing(const core::YamlNode& listing);

// The listing of a disassembly: the same as reading back scriptToYaml's YAML.
ScriptListing listingOf(const Disassembly& disassembly);

} // namespace dwe::formats::script
