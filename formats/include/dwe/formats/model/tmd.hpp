#pragma once

#include <dwe/core/error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dwe::formats::model {

// Bits of a primitive's mode byte. Polygons are `001 IIP QUAD TME ABE TGE`; the top three bits are the kind.
inline constexpr std::uint8_t TmdModeKindShift = 5;
inline constexpr std::uint8_t TmdKindPolygon = 1;
inline constexpr std::uint8_t TmdModeGouraud = 0x10;         // IIP: a normal or a colour per vertex
inline constexpr std::uint8_t TmdModeQuad = 0x08;            // four vertices instead of three
inline constexpr std::uint8_t TmdModeTextured = 0x04;        // TME
inline constexpr std::uint8_t TmdModeSemiTransparent = 0x02; // ABE
inline constexpr std::uint8_t TmdModeRawTexture = 0x01;      // TGE: texels are not shaded

// Bits of a primitive's flag byte.
inline constexpr std::uint8_t TmdFlagNoLight = 0x01;     // LGT: colours are used as they are, no normals
inline constexpr std::uint8_t TmdFlagDoubleSided = 0x02; // FCE
inline constexpr std::uint8_t TmdFlagGradation = 0x04;   // GRD: a colour per vertex on lit untextured polygons

// A vertex or a normal: an SVECTOR without its padding. Normals are unit length at 4096.
using TmdVector = std::array<std::int16_t, 3>;

struct TmdUv {
    std::uint8_t u = 0;
    std::uint8_t v = 0;

    bool operator==(const TmdUv&) const = default;
};

struct TmdColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    bool operator==(const TmdColor&) const = default;
};

// A polygon of an object, with its indices into the object's vertices and normals.
struct TmdPrimitive {
    std::uint8_t mode = 0;
    std::uint8_t flag = 0;
    std::uint8_t vertexCount = 0; // 3 or 4
    std::uint8_t normalCount = 0; // 0 (unlit), 1 (flat) or vertexCount (gouraud)
    std::uint8_t colorCount = 0;  // 0 (lit textured), 1, or vertexCount
    std::array<std::uint16_t, 4> vertices{};
    std::array<std::uint16_t, 4> normals{};
    std::array<TmdColor, 4> colors{};
    std::array<TmdUv, 4> uvs{};
    std::uint16_t cba = 0; // CLUT position: x / 16 in bits 0-5, y in bits 6-14
    std::uint16_t tsb = 0; // texture page: x / 64 in bits 0-3, y / 256 in bit 4, blending in 5-6, depth in 7-8

    [[nodiscard]] bool isTextured() const { return (mode & TmdModeTextured) != 0; }
    [[nodiscard]] bool isSemiTransparent() const { return (mode & TmdModeSemiTransparent) != 0; }
    [[nodiscard]] bool isLit() const { return (flag & TmdFlagNoLight) == 0; }
    [[nodiscard]] bool isDoubleSided() const { return (flag & TmdFlagDoubleSided) != 0; }

    // The texture page's corner in VRAM: x in halfwords, y in rows.
    [[nodiscard]] std::uint16_t pageX() const { return static_cast<std::uint16_t>((tsb & 0x0FU) * 64U); }
    [[nodiscard]] std::uint16_t pageY() const { return static_cast<std::uint16_t>(((tsb >> 4U) & 1U) * 256U); }
    // The semi-transparency mode of the page (ABR).
    [[nodiscard]] std::uint8_t blending() const { return static_cast<std::uint8_t>((tsb >> 5U) & 3U); }
    // The colour depth of the page: 0 = 4-bit, 1 = 8-bit, 2 = 16-bit, 3 = reserved.
    [[nodiscard]] std::uint8_t depth() const { return static_cast<std::uint8_t>((tsb >> 7U) & 3U); }
    // The palette's position in VRAM: x in halfwords, y in rows.
    [[nodiscard]] std::uint16_t clutX() const { return static_cast<std::uint16_t>((cba & 0x3FU) * 16U); }
    [[nodiscard]] std::uint16_t clutY() const { return static_cast<std::uint16_t>((cba >> 6U) & 0x1FFU); }
};

// The colour depth of 16-bit texture pages, which read colours instead of palette indices.
inline constexpr std::uint8_t TmdDirectColorDepth = 2;

struct TmdObject {
    std::vector<TmdVector> vertices;
    // The object's normals, plus any past its count that a primitive uses (see Tmd::notes and Tmd::issues).
    std::vector<TmdVector> normals;
    std::vector<TmdPrimitive> primitives;
    std::int32_t scale = 0; // stored, but the original's renderer ignores it
};

// Something unusual in the file, by object and primitive.
struct TmdIssue {
    std::uint32_t object = 0;
    std::uint32_t primitive = 0;
    std::string message;
};

// A model in the console's TMD format (`TMDModel`, include/dw/model.h in the decomp).
struct Tmd {
    std::uint32_t flags = 0;
    std::vector<TmdObject> objects;
    // What the reader could not take as it is: a primitive it skipped, or normals past an object's count that are not
    // the normals of another object.
    std::vector<TmdIssue> issues;
    // Unusual data the reader takes as the console does: normals past an object's count that are other objects'.
    std::vector<TmdIssue> notes;
};

// Reads a TMD whose offsets are relative to its object table, as every TMD of the original is.
core::Result<Tmd> readTmd(std::span<const std::byte> bytes);

} // namespace dwe::formats::model
