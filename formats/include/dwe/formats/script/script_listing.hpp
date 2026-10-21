#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/script/disassembly.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The control-flow view of a script listing (script_yaml.hpp): what each instruction does to the flow, read back from
// the YAML that dwx writes and the map editor edits. The operands that do not change the flow stay in the YAML.
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
};

struct ListingSection {
    std::uint16_t id = 0;
    std::uint32_t entry = 0; // the offset of its first instruction
};

struct ScriptListing {
    std::vector<ListingSection> sections;
    std::vector<ListingInstruction> instructions; // in offset order

    // The index of the instruction at an offset, if one starts there.
    [[nodiscard]] std::optional<std::size_t> indexAt(std::uint32_t offset) const;
};

// The flow role of an op name ("jump_to"); Plain for the ops that do not change the flow.
FlowRole flowRoleOf(std::string_view op);

// Reads the control-flow view of a listing. Fails on a listing that is not one: a missing `at` or `op`, a label that
// is not l_<hex>, instructions out of order, or a section or target that is not an instruction.
core::Result<ScriptListing> readScriptListing(const core::YamlNode& listing);

} // namespace dwe::formats::script
