#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

// Synthetic field map files (.MAP and .TFS) for the tests of the map readers and of the extraction tool.
namespace dwe::formats::test {

// Appends little-endian fields.
class Writer {
public:
    void i16(std::int64_t value) { put(static_cast<std::uint32_t>(value), 2); }
    void i32(std::int64_t value) { put(static_cast<std::uint32_t>(value), 4); }
    void u8(std::uint8_t value) { bytes_.push_back(std::byte{value}); }
    void fill(std::size_t count, std::uint8_t value) { bytes_.insert(bytes_.end(), count, std::byte{value}); }
    void append(const std::vector<std::byte>& other) { bytes_.insert(bytes_.end(), other.begin(), other.end()); }
    void setI32(std::size_t offset, std::int64_t value) {
        for (std::size_t i = 0; i < 4; ++i) {
            bytes_[offset + i] = static_cast<std::byte>((static_cast<std::uint32_t>(value) >> (8U * i)) & 0xFFU);
        }
    }
    [[nodiscard]] std::size_t size() const { return bytes_.size(); }
    [[nodiscard]] const std::vector<std::byte>& bytes() const { return bytes_; }

private:
    void put(std::uint32_t value, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) {
            bytes_.push_back(static_cast<std::byte>((value >> (8U * i)) & 0xFFU));
        }
    }

    std::vector<std::byte> bytes_;
};

// The parts of a synthetic map, so that tests can break one of them.
struct MapParts {
    std::int32_t gridWidth = 2;
    std::int32_t gridHeight = 1;
    std::vector<std::int32_t> tiles = {0, -1};
    std::int16_t spriteCount = 1;
    std::int16_t instanceCount = 1;
    std::int16_t npcCount = 1;
    std::int16_t waypointCount = 2;
    std::size_t collisionSize = 10000;
    // The TIM sections; the reader does not look inside them.
    std::vector<std::byte> image8 = std::vector<std::byte>(16, std::byte{0x10});
    std::vector<std::byte> image4 = std::vector<std::byte>(8, std::byte{0x10});
};

inline std::vector<std::byte> setupSection(const MapParts& parts) {
    Writer w;
    for (int i = 0; i < 6; ++i) {
        w.i32(std::int64_t{100} + i); // camera
    }
    for (int light = 0; light < 3; ++light) {
        w.i32(-4096);
        w.i32(light);
        w.i32(0);
        w.i32(128);
        w.i32(64);
        w.i32(32);
    }
    w.i32(1);
    w.i32(2);
    w.i32(3); // ambient
    w.i32(512);
    for (int i = 0; i < 8; ++i) {
        w.i32(i); // regions
    }
    w.i32(parts.gridWidth);
    w.i32(parts.gridHeight);
    for (const std::int32_t tile : parts.tiles) {
        w.i32(tile);
    }
    return w.bytes();
}

inline std::vector<std::byte> objectsSection(const MapParts& parts) {
    Writer w;
    w.i16(parts.spriteCount);
    for (int i = 0; i < std::min<int>(parts.spriteCount, 2); ++i) {
        for (const int value : {256, 16, 32, 48, 10, 20, 30, -1, 4}) {
            w.i16(value);
        }
    }
    w.i16(parts.instanceCount);
    for (int i = 0; i < std::min<int>(parts.instanceCount, 2); ++i) {
        for (const int sprite : {0, 0, -1, 0, 0, 0, 0, 0}) {
            w.i16(sprite);
        }
        for (const int time : {5, 6, 0, 0, 0, 0, 0, 0}) {
            w.i16(time);
        }
        w.i16(64);
        w.i16(80);
        w.i16(1);
    }
    return w.bytes();
}

inline std::vector<std::byte> entitiesSection(const MapParts& parts) {
    Writer w;
    for (int field = 0; field < 6; ++field) {
        for (int exit = 0; exit < 10; ++exit) {
            if (field < 4) {
                w.i16((field * 100) + exit); // spawn and rotation
            } else if (exit == 0) {
                w.i16(field == 4 ? 1 : 0); // exit 0 leads to map 1, exit 0
            } else {
                w.i16(-1);
            }
        }
    }
    w.i16(parts.npcCount);
    for (int npc = 0; npc < std::min<int>(parts.npcCount, 2); ++npc) {
        w.i16(2); // typeId
        for (int i = 1; i < 33; ++i) {
            w.i16(i);
        }
        w.i16(parts.waypointCount);
        for (int i = 0; i < 8; ++i) {
            w.i16(-1); // aiSections
        }
        for (int i = 0; i < std::min<int>(parts.waypointCount, 2) * 3; ++i) {
            w.i16(std::int64_t{i} * 10);
        }
    }
    return w.bytes();
}

inline std::vector<std::byte> collisionSection(const MapParts& parts) {
    std::vector<std::byte> cells(parts.collisionSize, std::byte{0});
    if (cells.size() >= 10000) {
        cells[0] = std::byte{0x80};
        cells[101] = std::byte{51};
        cells[9999] = std::byte{120};
    }
    return cells;
}

// A map with one 8-bit TIM, one 4-bit TIM, objects, one NPC and the collision grid.
inline std::vector<std::byte> makeMap(const MapParts& parts = {}) {
    const std::vector<std::vector<std::byte>> sections = {
        setupSection(parts),   parts.image8,           parts.image4,
        objectsSection(parts), entitiesSection(parts), collisionSection(parts),
    };
    Writer w;
    std::size_t offset = sections.size() * 4;
    for (const auto& section : sections) {
        w.i32(static_cast<std::int64_t>(offset));
        offset += section.size();
    }
    for (const auto& section : sections) {
        w.append(section);
    }
    return w.bytes();
}

inline std::vector<std::byte> makeTfs(std::uint32_t paletteCount, std::size_t tileCount) {
    Writer w;
    w.i32(0x00800100);
    w.i32(paletteCount);
    for (std::uint32_t palette = 0; palette < paletteCount; ++palette) {
        for (int colour = 0; colour < 256; ++colour) {
            w.i16((std::int64_t{palette} * 1000) + colour);
        }
    }
    for (std::size_t tile = 0; tile < tileCount; ++tile) {
        w.i32(0);
        for (std::size_t pixel = 0; pixel < std::size_t{128} * 128; ++pixel) {
            w.u8(static_cast<std::uint8_t>((pixel % 128) + tile));
        }
    }
    return w.bytes();
}

} // namespace dwe::formats::test
