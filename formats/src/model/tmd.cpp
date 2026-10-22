#include <dwe/formats/byte_reader.hpp>
#include <dwe/formats/model/tmd.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dwe::formats::model {

namespace {

constexpr std::uint32_t TmdId = 0x41;
constexpr std::uint32_t TmdFlagAbsoluteOffsets = 1;
constexpr std::size_t HeaderSize = 12;
constexpr std::size_t ObjectHeaderSize = 28;
constexpr std::size_t VectorSize = 8;
constexpr std::size_t WordSize = 4;

// Where each part of a polygon packet is, from the PsyQ TMD layouts: texture coordinates first,
// then colours, then the normal and vertex indices.
struct PolygonLayout {
  std::uint8_t vertexCount = 0;
  std::uint8_t uvWords = 0;
  std::uint8_t colorWords = 0;
  std::uint8_t normalCount = 0;
  std::uint8_t indexWords = 0;

  [[nodiscard]] std::uint32_t words() const {
    return std::uint32_t{uvWords} + colorWords + indexWords;
  }
};

PolygonLayout polygonLayout(std::uint8_t mode, std::uint8_t flag) {
  const bool lit = (flag & TmdFlagNoLight) == 0;
  const bool textured = (mode & TmdModeTextured) != 0;
  const bool gouraud = (mode & TmdModeGouraud) != 0;
  const std::uint8_t count = (mode & TmdModeQuad) != 0 ? 4 : 3;

  PolygonLayout layout{.vertexCount = count, .uvWords = textured ? count : std::uint8_t{0}};
  if (lit) {
    const bool gradation = !textured && (flag & TmdFlagGradation) != 0;
    if (!textured) {
      layout.colorWords = gradation ? count : 1;
    }
    layout.normalCount = gouraud ? count : 1;
    // Gouraud: one (normal, vertex) pair per word. Flat: (normal, vertex 0), then the other
    // vertices two per word.
    layout.indexWords = gouraud ? count : static_cast<std::uint8_t>(1 + (count / 2));
  } else {
    layout.colorWords = gouraud ? count : 1;
    layout.indexWords = 2;
  }
  return layout;
}

std::uint16_t lowHalf(std::uint32_t word) {
  return static_cast<std::uint16_t>(word & 0xFFFFU);
}

std::uint16_t highHalf(std::uint32_t word) {
  return static_cast<std::uint16_t>(word >> 16U);
}

TmdPrimitive decodePolygon(const ByteReader& packet, std::uint8_t mode, std::uint8_t flag,
                           const PolygonLayout& layout) {
  TmdPrimitive primitive{.mode = mode,
                         .flag = flag,
                         .vertexCount = layout.vertexCount,
                         .normalCount = layout.normalCount,
                         .colorCount = layout.colorWords};
  std::size_t word = 0;
  // The caller checked that the packet has layout.words() words.
  const auto next = [&] { return packet.u32(WordSize * word++).value_or(0); };

  for (std::size_t i = 0; i < layout.uvWords; ++i) {
    const std::uint32_t value = next();
    primitive.uvs[i] = {.u = static_cast<std::uint8_t>(value),
                        .v = static_cast<std::uint8_t>(value >> 8U)};
    if (i == 0) {
      primitive.cba = highHalf(value);
    } else if (i == 1) {
      primitive.tsb = highHalf(value);
    }
  }
  for (std::size_t i = 0; i < layout.colorWords; ++i) {
    const std::uint32_t value = next();
    primitive.colors[i] = {.r = static_cast<std::uint8_t>(value),
                           .g = static_cast<std::uint8_t>(value >> 8U),
                           .b = static_cast<std::uint8_t>(value >> 16U)};
  }

  if (layout.normalCount == layout.vertexCount) {
    for (std::size_t i = 0; i < layout.vertexCount; ++i) {
      const std::uint32_t value = next();
      primitive.normals[i] = lowHalf(value);
      primitive.vertices[i] = highHalf(value);
    }
    return primitive;
  }
  std::size_t vertex = 0;
  if (layout.normalCount == 1) {
    const std::uint32_t value = next();
    primitive.normals[0] = lowHalf(value);
    primitive.vertices[vertex++] = highHalf(value);
  }
  while (vertex < layout.vertexCount) {
    const std::uint32_t value = next();
    primitive.vertices[vertex++] = lowHalf(value);
    if (vertex < layout.vertexCount) {
      primitive.vertices[vertex++] = highHalf(value);
    }
  }
  return primitive;
}

// Reads count vectors whose range the caller checked.
std::vector<TmdVector> readVectors(const ByteReader& reader, std::size_t offset,
                                   std::size_t count) {
  const auto s16 = [&](std::size_t at) {
    return static_cast<std::int16_t>(reader.u16(at).value_or(0));
  };
  std::vector<TmdVector> vectors(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t at = offset + (i * VectorSize);
    vectors[i] = {s16(at), s16(at + 2), s16(at + 4)};
  }
  return vectors;
}

struct ObjectHeader {
  std::uint32_t vertTop = 0;
  std::uint32_t nVert = 0;
  std::uint32_t normalTop = 0;
  std::uint32_t nNormal = 0;
  std::uint32_t primitiveTop = 0;
  std::uint32_t nPrimitive = 0;
  std::int32_t scale = 0;
};

// The objects whose normals hold the normals of an object from `first` to `end`, if every one of
// them is the whole normal of another object.
std::optional<std::vector<std::uint32_t>> normalOwners(std::span<const ObjectHeader> headers,
                                                       std::uint32_t index, std::size_t first,
                                                       std::size_t end) {
  std::vector<std::uint32_t> owners;
  for (std::size_t normal = first; normal < end; ++normal) {
    const std::size_t at = headers[index].normalTop + (normal * VectorSize);
    const auto owner = std::ranges::find_if(headers, [&](const ObjectHeader& other) {
      return &other != &headers[index] && at >= other.normalTop &&
             at + VectorSize <= other.normalTop + (std::size_t{other.nNormal} * VectorSize) &&
             (at - other.normalTop) % VectorSize == 0;
    });
    if (owner == headers.end()) {
      return std::nullopt;
    }
    const auto ownerIndex = static_cast<std::uint32_t>(owner - headers.begin());
    if (!std::ranges::contains(owners, ownerIndex)) {
      owners.push_back(ownerIndex);
    }
  }
  std::ranges::sort(owners);
  return owners;
}

core::Result<TmdObject> readObject(const ByteReader& table, std::span<const ObjectHeader> headers,
                                   std::uint32_t index, Tmd& tmd) {
  const ObjectHeader& header = headers[index];
  if (!table.contains(header.vertTop, std::size_t{header.nVert} * VectorSize)) {
    return core::fail(std::format("object {}: {} vertices at {:#x} go past the end", index,
                                  header.nVert, header.vertTop));
  }
  if (!table.contains(header.normalTop, std::size_t{header.nNormal} * VectorSize)) {
    return core::fail(std::format("object {}: {} normals at {:#x} go past the end", index,
                                  header.nNormal, header.normalTop));
  }

  TmdObject object{.vertices = readVectors(table, header.vertTop, header.nVert),
                   .normals = {},
                   .primitives = {},
                   .scale = header.scale};
  std::size_t normalsUsed = header.nNormal;
  std::size_t at = header.primitiveTop;
  for (std::uint32_t p = 0; p < header.nPrimitive; ++p) {
    const auto header0 = table.u32(at);
    if (!header0) {
      return core::fail(
          std::format("object {}: primitive {} at {:#x} is past the end", index, p, at));
    }
    const auto ilen = static_cast<std::uint8_t>(*header0 >> 8U);
    const auto flag = static_cast<std::uint8_t>(*header0 >> 16U);
    const auto mode = static_cast<std::uint8_t>(*header0 >> 24U);
    const std::size_t packetSize = std::size_t{ilen} * WordSize;
    const auto packet = table.bytes(at + WordSize, packetSize);
    if (!packet) {
      return core::fail(
          std::format("object {}: primitive {} ({} words) goes past the end", index, p, ilen));
    }
    at += WordSize + packetSize;

    const PolygonLayout layout = polygonLayout(mode, flag);
    if ((mode >> TmdModeKindShift) != TmdKindPolygon || layout.words() != ilen) {
      tmd.issues.push_back(
          {.object = index,
           .primitive = p,
           .message = std::format("skipped primitive with mode {:#04x}, flag {:#04x}, {} words",
                                  mode, flag, ilen)});
      continue;
    }

    TmdPrimitive primitive = decodePolygon(ByteReader{*packet}, mode, flag, layout);
    for (std::size_t i = 0; i < primitive.vertexCount; ++i) {
      if (primitive.vertices[i] >= header.nVert) {
        return core::fail(std::format("object {}: primitive {} uses vertex {} of {}", index, p,
                                      primitive.vertices[i], header.nVert));
      }
    }
    for (std::size_t i = 0; i < primitive.normalCount; ++i) {
      normalsUsed = std::max<std::size_t>(normalsUsed, std::size_t{primitive.normals[i]} + 1);
    }
    object.primitives.push_back(primitive);
  }

  // The console indexes normals without checking the count (libgs GsTMDfast*, through
  // GsSortObject4J), so a primitive may use the bytes after the object's normals. One model of the
  // original does, and they are the normals of the objects that follow; the reader reads them from
  // there as long as they are in the file.
  if (normalsUsed > header.nNormal) {
    if (!table.contains(header.normalTop, normalsUsed * VectorSize)) {
      return core::fail(
          std::format("object {}: a primitive uses normal {}, past the end of the file", index,
                      normalsUsed - 1));
    }
    const auto owners = normalOwners(headers, index, header.nNormal, normalsUsed);
    if (owners) {
      std::string list;
      for (std::size_t i = 0; i < owners->size(); ++i) {
        if (i > 0) {
          list += i + 1 == owners->size() ? " and " : ", ";
        }
        list += std::to_string((*owners)[i]);
      }
      tmd.notes.push_back({.object = index,
                           .primitive = 0,
                           .message = std::format(
                               "primitives use {} normals but the object has {}; the extra "
                               "ones are normals of object{} {}, as the console reads them",
                               normalsUsed, header.nNormal, owners->size() == 1 ? "" : "s", list)});
    } else {
      tmd.issues.push_back(
          {.object = index,
           .primitive = 0,
           .message = std::format("primitives use {} normals but the object has {}; the extra "
                                  "ones are read from bytes that are not normals",
                                  normalsUsed, header.nNormal)});
    }
  }
  object.normals = readVectors(table, header.normalTop, normalsUsed);
  return object;
}

} // namespace

core::Result<Tmd> readTmd(std::span<const std::byte> bytes) {
  const ByteReader reader{bytes};
  const auto id = reader.u32(0);
  const auto flags = reader.u32(4);
  const auto objectCount = reader.u32(8);
  if (!id || !flags || !objectCount) {
    return core::fail(std::format("TMD of {} bytes is shorter than its header", bytes.size()));
  }
  if (*id != TmdId) {
    return core::fail(std::format("TMD id is {:#x}, not {:#x}", *id, TmdId));
  }
  if ((*flags & TmdFlagAbsoluteOffsets) != 0) {
    return core::fail("TMD uses absolute addresses; only relative offsets can be read from a file");
  }
  Tmd tmd{.flags = *flags, .objects = {}, .issues = {}, .notes = {}};
  const ByteReader table{bytes.subspan(HeaderSize)};
  if (!table.contains(0, std::size_t{*objectCount} * ObjectHeaderSize)) {
    return core::fail(
        std::format("TMD object table of {} objects goes past the end", *objectCount));
  }

  std::vector<ObjectHeader> headers;
  headers.reserve(*objectCount);
  for (std::uint32_t i = 0; i < *objectCount; ++i) {
    // The table's range is checked above.
    const std::size_t at = std::size_t{i} * ObjectHeaderSize;
    const auto field = [&](std::size_t offset) { return table.u32(at + offset).value_or(0); };
    headers.push_back({.vertTop = field(0),
                       .nVert = field(4),
                       .normalTop = field(8),
                       .nNormal = field(12),
                       .primitiveTop = field(16),
                       .nPrimitive = field(20),
                       .scale = static_cast<std::int32_t>(field(24))});
  }
  for (std::uint32_t i = 0; i < *objectCount; ++i) {
    auto object = readObject(table, headers, i, tmd);
    if (!object) {
      return core::fail(std::format("TMD {}", object.error().message));
    }
    tmd.objects.push_back(std::move(*object));
  }
  return tmd;
}

} // namespace dwe::formats::model
