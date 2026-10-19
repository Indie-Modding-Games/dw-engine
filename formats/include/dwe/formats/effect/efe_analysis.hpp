#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/effect/efe_file.hpp>
#include <dwe/formats/effect/efe_script.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
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
    Unreferenced, // bytes nothing addresses that are not fill: data the scripts never use, as far as they show
};

struct EfeDataItem {
    std::uint32_t offset = 0; // from the slot's base
    std::uint32_t size = 0;   // 0 when the extent does not follow from the scripts
    EfeDataKind kind = EfeDataKind::Fill;
    EfeWidth width = EfeWidth::Int32;       // Variable, Table
    EfeArgKind argKind = EfeArgKind::Value; // Argument
    std::int16_t count = 0;                 // Instances
    std::int16_t stride = 0;                // Instances

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
    // variable read as part of a vector; gaps cover what no item does.
    std::vector<EfeDataItem> data;
    // What could not be followed: targets between instructions, addresses outside the data area, extents that run
    // past the slot, unreferenced bytes. Each one is a thing the extraction does not understand.
    std::vector<std::string> issues;
};

// Follows the slot's scripts: labels, the pushes behind every sub-op argument, and the items of the data area.
core::Result<EfeAnalysis> analyzeEfeScripts(std::span<const std::byte> payload, const EfeHeader& header,
                                            const EfeScripts& scripts);

} // namespace dwe::formats::effect
