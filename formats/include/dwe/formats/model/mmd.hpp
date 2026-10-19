#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/model/tmd.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dwe::formats::model {

// A model with its animations (`CHDAT\MMDn\*.MMD`, read by model.c: loadMMD in the decomp): `u32 tmdOffset`,
// `u32 animTableOffset`, the TMD, then a table of `u32` offsets to the animation streams, relative to the table.
struct Mmd {
    Tmd tmd;
    std::uint32_t animTableOffset = 0;
    // Where each animation's stream starts in the file, by animation id; empty where the table has 0 (no animation).
    std::vector<std::optional<std::uint32_t>> animations;
};

// Reads an MMD. The table has no count: it ends where the first stream starts, so the count is the smallest non-zero
// offset divided by 4.
core::Result<Mmd> readMmd(std::span<const std::byte> bytes);

} // namespace dwe::formats::model
