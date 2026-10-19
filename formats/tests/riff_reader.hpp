#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Reads back the RIFF files the writers make (WAV, SF2), checking every chunk size, for the tests.
namespace dwe::formats::audio::test {

struct RiffChunk {
    std::string id;
    std::string type; // the form type of RIFF and LIST chunks
    std::span<const std::byte> data;
    std::vector<RiffChunk> children;

    [[nodiscard]] const RiffChunk* child(std::string_view name) const {
        for (const auto& chunk : children) {
            if (chunk.id == name || (chunk.id == "LIST" && chunk.type == name)) {
                return &chunk;
            }
        }
        return nullptr;
    }
};

inline std::uint32_t readU32(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8U * i);
    }
    return value;
}

inline std::uint16_t readU16(std::span<const std::byte> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(std::to_integer<std::uint32_t>(bytes[offset]) |
                                      (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U));
}

inline std::string readTag(std::span<const std::byte> bytes, std::size_t offset) {
    std::string tag;
    for (std::size_t i = 0; i < 4; ++i) {
        tag.push_back(static_cast<char>(bytes[offset + i]));
    }
    return tag;
}

// Reads the chunks that fill `bytes` exactly (each padded to an even size). Nothing when a size does not fit.
inline std::optional<std::vector<RiffChunk>> parseChunks(std::span<const std::byte> bytes) {
    std::vector<RiffChunk> chunks;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        if (bytes.size() - offset < 8) {
            return std::nullopt;
        }
        RiffChunk chunk{.id = readTag(bytes, offset), .type = {}, .data = {}, .children = {}};
        const std::size_t size = readU32(bytes, offset + 4);
        if (size > bytes.size() - offset - 8) {
            return std::nullopt;
        }
        chunk.data = bytes.subspan(offset + 8, size);
        if (chunk.id == "RIFF" || chunk.id == "LIST") {
            if (size < 4) {
                return std::nullopt;
            }
            chunk.type = readTag(chunk.data, 0);
            auto children = parseChunks(chunk.data.subspan(4));
            if (!children) {
                return std::nullopt;
            }
            chunk.children = std::move(*children);
        }
        offset += 8 + size + (size % 2);
        chunks.push_back(std::move(chunk));
    }
    if (offset != bytes.size()) {
        return std::nullopt;
    }
    return chunks;
}

// Reads a whole RIFF file: one RIFF chunk that takes every byte. A chunk with an empty id when it is not one.
inline RiffChunk parseRiff(std::span<const std::byte> bytes) {
    auto chunks = parseChunks(bytes);
    if (!chunks || chunks->size() != 1 || (*chunks)[0].id != "RIFF") {
        return RiffChunk{.id = {}, .type = {}, .data = {}, .children = {}};
    }
    return std::move((*chunks)[0]);
}

} // namespace dwe::formats::audio::test
