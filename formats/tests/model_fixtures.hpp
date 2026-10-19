#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

// Builders for the synthetic models of the tests: little-endian bytes written by hand, never taken from the disc.
namespace dwe::formats::model::testing {

class ByteWriter {
public:
    void u8(std::uint32_t value) { bytes_.push_back(static_cast<std::byte>(value & 0xFFU)); }
    void u16(std::uint32_t value) {
        u8(value);
        u8(value >> 8U);
    }
    void s16(std::int32_t value) { u16(static_cast<std::uint32_t>(value)); }
    void u32(std::uint32_t value) {
        u16(value);
        u16(value >> 16U);
    }
    void words(std::initializer_list<std::int32_t> values) {
        for (const std::int32_t value : values) {
            s16(value);
        }
    }
    void append(std::span<const std::byte> bytes) { bytes_.insert(bytes_.end(), bytes.begin(), bytes.end()); }
    void patchU32(std::size_t offset, std::uint32_t value) {
        for (std::size_t i = 0; i < 4; ++i) {
            bytes_[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
        }
    }

    [[nodiscard]] std::size_t size() const { return bytes_.size(); }
    [[nodiscard]] const std::vector<std::byte>& bytes() const { return bytes_; }

private:
    std::vector<std::byte> bytes_;
};

// The first word of a primitive packet: olen, ilen, flag, mode.
inline std::uint32_t primitiveHeader(std::uint8_t mode, std::uint8_t flag, std::uint8_t ilen) {
    return std::uint32_t{ilen} << 8U | std::uint32_t{flag} << 16U | std::uint32_t{mode} << 24U;
}

inline std::uint32_t pair(std::uint32_t low, std::uint32_t high) {
    return low | (high << 16U);
}

inline std::uint32_t uvWord(std::uint32_t u, std::uint32_t v, std::uint32_t high) {
    return u | (v << 8U) | (high << 16U);
}

inline std::uint32_t colorWord(std::uint32_t r, std::uint32_t g, std::uint32_t b) {
    return r | (g << 8U) | (b << 16U);
}

struct TestObject {
    std::vector<std::int16_t> vertices;                 // x, y, z per vertex
    std::vector<std::int16_t> normals;                  // x, y, z per normal
    std::vector<std::vector<std::uint32_t>> primitives; // header word, then the packet
};

// Builds a TMD with relative offsets: header, object table, then each object's vertices, normals and primitives.
inline std::vector<std::byte> buildTmd(const std::vector<TestObject>& objects) {
    ByteWriter table;
    ByteWriter data;
    const std::size_t dataStart = objects.size() * 28;
    for (const TestObject& object : objects) {
        const auto vertTop = static_cast<std::uint32_t>(dataStart + data.size());
        for (std::size_t i = 0; i < object.vertices.size(); i += 3) {
            data.words({object.vertices[i], object.vertices[i + 1], object.vertices[i + 2], 0});
        }
        const auto normalTop = static_cast<std::uint32_t>(dataStart + data.size());
        for (std::size_t i = 0; i < object.normals.size(); i += 3) {
            data.words({object.normals[i], object.normals[i + 1], object.normals[i + 2], 0});
        }
        const auto primitiveTop = static_cast<std::uint32_t>(dataStart + data.size());
        for (const auto& primitive : object.primitives) {
            for (const std::uint32_t word : primitive) {
                data.u32(word);
            }
        }
        table.u32(vertTop);
        table.u32(static_cast<std::uint32_t>(object.vertices.size() / 3));
        table.u32(normalTop);
        table.u32(static_cast<std::uint32_t>(object.normals.size() / 3));
        table.u32(primitiveTop);
        table.u32(static_cast<std::uint32_t>(object.primitives.size()));
        table.u32(0);
    }
    ByteWriter tmd;
    tmd.u32(0x41);
    tmd.u32(0);
    tmd.u32(static_cast<std::uint32_t>(objects.size()));
    tmd.append(table.bytes());
    tmd.append(data.bytes());
    return tmd.bytes();
}

// Two objects: a textured gouraud triangle and a textured flat double-sided quad, then an unlit flat quad, a lit
// triangle with a colour per vertex and a line (which the reader skips and reports).
inline std::vector<std::byte> buildSampleTmd() {
    TestObject first{
        .vertices = {0, 0, 0, 100, 0, 0, 0, -100, 0, 100, -100, 0},
        .normals = {0, 0, -4096, 0, -4096, 0},
        .primitives =
            {
                {primitiveHeader(0x34, 0, 6), uvWord(0, 0, 0x7A40), uvWord(255, 0, 0x0005), uvWord(0, 127, 0),
                 pair(0, 0), pair(0, 1), pair(1, 2)},
                {primitiveHeader(0x2C, 0x02, 7), uvWord(0, 0, 0x7A00), uvWord(16, 0, 0x0025), uvWord(0, 16, 0),
                 uvWord(16, 16, 0), pair(0, 0), pair(1, 2), pair(3, 0)},
            },
    };
    TestObject second{
        .vertices = {0, 0, 0, 50, 0, 0, 0, 50, 0, 50, 50, 0},
        .normals = {4096, 0, 0},
        .primitives =
            {
                {primitiveHeader(0x28, 0x01, 3), colorWord(10, 20, 30), pair(0, 1), pair(2, 3)},
                {primitiveHeader(0x20, 0x04, 5), colorWord(255, 0, 0), colorWord(0, 255, 0), colorWord(0, 0, 255),
                 pair(0, 0), pair(1, 2)},
                {primitiveHeader(0x40, 0x01, 2), colorWord(1, 2, 3), pair(0, 1)},
            },
    };
    return buildTmd({first, second});
}

} // namespace dwe::formats::model::testing
