#include <dwe/formats/byte_reader.hpp>
#include <dwe/formats/model/mmd.hpp>

#include <algorithm>
#include <format>
#include <limits>

namespace dwe::formats::model {

namespace {

constexpr std::size_t HeaderSize = 8;
constexpr std::size_t EntrySize = 4;
constexpr std::uint32_t StreamAlignment = 2;

} // namespace

core::Result<Mmd> readMmd(std::span<const std::byte> bytes) {
    const ByteReader reader{bytes};
    const auto tmdOffsetField = reader.u32(0);
    const auto animTableOffsetField = reader.u32(4);
    if (!tmdOffsetField || !animTableOffsetField) {
        return core::fail(std::format("MMD of {} bytes is shorter than its header", bytes.size()));
    }
    const std::uint32_t tmdOffset = *tmdOffsetField;
    const std::uint32_t animTableOffset = *animTableOffsetField;
    if (tmdOffset < HeaderSize || animTableOffset <= tmdOffset || animTableOffset > bytes.size()) {
        return core::fail(std::format("MMD offsets {:#x} (model) and {:#x} (animations) do not fit a file of {} bytes",
                                      tmdOffset, animTableOffset, bytes.size()));
    }
    auto tmd = readTmd(bytes.subspan(tmdOffset, animTableOffset - tmdOffset));
    if (!tmd) {
        return core::fail(std::format("MMD model: {}", tmd.error().message));
    }

    Mmd mmd{.tmd = std::move(*tmd), .animTableOffset = animTableOffset, .animations = {}};
    const ByteReader table{bytes.subspan(animTableOffset)};
    std::uint32_t tableEnd = std::numeric_limits<std::uint32_t>::max();
    for (std::size_t at = 0; at < tableEnd; at += EntrySize) {
        const auto entry = table.u32(at);
        if (!entry) {
            return core::fail("MMD animation table runs to the end of the file without a stream");
        }
        const std::uint32_t offset = *entry;
        if (offset == 0) {
            mmd.animations.emplace_back();
            continue;
        }
        if (offset < at + EntrySize || offset >= table.size() || (animTableOffset + offset) % StreamAlignment != 0) {
            return core::fail(std::format("MMD animation {} has offset {:#x}, outside the file or inside the table",
                                          mmd.animations.size(), offset));
        }
        tableEnd = std::min(tableEnd, offset);
        mmd.animations.emplace_back(animTableOffset + offset);
    }
    return mmd;
}

} // namespace dwe::formats::model
