#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/effect/efe_file.hpp>
#include <dwe/formats/effect/efe_script.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// What a slot's scripts do with its data area, [0, startScript): which bytes they address and as what. The original
// has no table of its data; the scripts are the only key to it.
namespace dwe::formats::effect {

enum class EfeDataKind : std::uint8_t {
    Instances,    // a spawn's table of EfeInstance records, each followed by the sub-effect's own variables
    Variable,     // a variable an instruction reads or writes
    Table,        // a table load_indexed reads; its length is not stored, it runs up to the next item
    Argument,     // what a sub-op argument points at (EfeArg::kind says what)
    Fill,         // bytes nothing addresses, all fill (0xCD): room the tool left
    Unused,       // bytes nothing reaches that are not fill: data the tool left and the scripts never use
    Unreferenced, // bytes nothing addresses that a read of unknown extent before them may reach: not understood
};

// Why a gap of data that is not fill is unused (BTL_runEFEScript and its handlers address the data area only so).
inline constexpr std::string_view EfeUnusedReason =
    "nothing reaches it: the interpreter addresses data only through instruction operands, load_indexed tables and "
    "sub-op pointers";

struct EfeDataItem {
    std::uint32_t offset = 0; // from the slot's base
    std::uint32_t size = 0;   // 0 when the extent does not follow from the scripts
    EfeDataKind kind = EfeDataKind::Fill;
    EfeWidth width = EfeWidth::Int32;       // Variable, Table
    EfeArgKind argKind = EfeArgKind::Value; // Argument
    std::int16_t count = 0;                 // Instances; ClutRow: the colour indices a row can start at
    std::int16_t stride = 0;                // Instances
    // False when the handler reads as far as an argument says and the scripts do not push that argument as a number
    // or bound it: the read has no known end.
    bool bounded = true;

    bool operator==(const EfeDataItem&) const = default;
};

struct EfeAnalysis {
    // Offsets of the instructions that jumps, branches, calls and code arguments go to, ascending.
    std::vector<std::uint32_t> labels;
    // Per instruction: for a sub-op, the index of the instruction that pushed each argument, in push order; for
    // pop_to_variable, the one value it pops. Empty for other instructions. A value pushed before a label or a call,
    // such as an argument a caller passes, has no index.
    std::vector<std::vector<std::optional<std::size_t>>> arguments;
    // The data area: what the scripts address, then the gaps between, sorted by offset. Items can nest, such as a
    // variable read as part of a vector; gaps cover what no item does. The interpreter forms a data address only
    // from an instruction's operand, a load_indexed index or a sub-op's pointer argument, so a gap no item covers is
    // unused unless a read of unknown extent starts before it.
    std::vector<EfeDataItem> data;
    // What could not be followed: targets between instructions, addresses outside the data area, extents that run
    // past the slot, bytes a read of unknown extent may reach. Each one is a thing the extraction does not understand.
    std::vector<std::string> issues;
};

// Follows the slot's scripts: labels, the pushes behind every sub-op argument, and the items of the data area.
core::Result<EfeAnalysis> analyzeEfeScripts(std::span<const std::byte> payload, const EfeHeader& header,
                                            const EfeScripts& scripts);

} // namespace dwe::formats::effect
