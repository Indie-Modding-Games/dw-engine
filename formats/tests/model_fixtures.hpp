#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

// Builders for the synthetic models of the tests: little-endian bytes written by hand, never taken
// from the disc.
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
  void append(std::span<const std::byte> bytes) {
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
  }
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

// How buildTmd lays out the objects' data.
enum class TmdLayout : std::uint8_t {
  ByObject, // each object's vertices, normals and primitives, object after object
  ByKind,   // every object's primitives, then every object's vertices, then every object's normals
};

// Builds a TMD with relative offsets: header, object table, then the objects' data.
inline std::vector<std::byte> buildTmd(const std::vector<TestObject>& objects,
                                       TmdLayout layout = TmdLayout::ByObject) {
  ByteWriter table;
  ByteWriter data;
  const std::size_t dataStart = objects.size() * 28;
  struct Tops {
    std::uint32_t vertices = 0;
    std::uint32_t normals = 0;
    std::uint32_t primitives = 0;
  };
  std::vector<Tops> tops(objects.size());
  const auto writeVertices = [&](std::size_t o) {
    tops[o].vertices = static_cast<std::uint32_t>(dataStart + data.size());
    const auto& vertices = objects[o].vertices;
    for (std::size_t i = 0; i < vertices.size(); i += 3) {
      data.words({vertices[i], vertices[i + 1], vertices[i + 2], 0});
    }
  };
  const auto writeNormals = [&](std::size_t o) {
    tops[o].normals = static_cast<std::uint32_t>(dataStart + data.size());
    const auto& normals = objects[o].normals;
    for (std::size_t i = 0; i < normals.size(); i += 3) {
      data.words({normals[i], normals[i + 1], normals[i + 2], 0});
    }
  };
  const auto writePrimitives = [&](std::size_t o) {
    tops[o].primitives = static_cast<std::uint32_t>(dataStart + data.size());
    for (const auto& primitive : objects[o].primitives) {
      for (const std::uint32_t word : primitive) {
        data.u32(word);
      }
    }
  };
  if (layout == TmdLayout::ByObject) {
    for (std::size_t o = 0; o < objects.size(); ++o) {
      writeVertices(o);
      writeNormals(o);
      writePrimitives(o);
    }
  } else {
    for (std::size_t o = 0; o < objects.size(); ++o) {
      writePrimitives(o);
    }
    for (std::size_t o = 0; o < objects.size(); ++o) {
      writeVertices(o);
    }
    for (std::size_t o = 0; o < objects.size(); ++o) {
      writeNormals(o);
    }
  }
  for (std::size_t o = 0; o < objects.size(); ++o) {
    const TestObject& object = objects[o];
    const auto [vertTop, normalTop, primitiveTop] = tops[o];
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

// Two objects: a textured gouraud triangle and a textured flat double-sided quad, then an unlit
// flat quad, a lit triangle with a colour per vertex and a line (which the reader skips and
// reports).
inline std::vector<std::byte> buildSampleTmd() {
  TestObject first{
      .vertices = {0, 0, 0, 100, 0, 0, 0, -100, 0, 100, -100, 0},
      .normals = {0, 0, -4096, 0, -4096, 0},
      .primitives =
          {
              {primitiveHeader(0x34, 0, 6), uvWord(0, 0, 0x7A40), uvWord(255, 0, 0x0005),
               uvWord(0, 127, 0), pair(0, 0), pair(0, 1), pair(1, 2)},
              {primitiveHeader(0x2C, 0x02, 7), uvWord(0, 0, 0x7A00), uvWord(16, 0, 0x0025),
               uvWord(0, 16, 0), uvWord(16, 16, 0), pair(0, 0), pair(1, 2), pair(3, 0)},
          },
  };
  TestObject second{
      .vertices = {0, 0, 0, 50, 0, 0, 0, 50, 0, 50, 50, 0},
      .normals = {4096, 0, 0},
      .primitives =
          {
              {primitiveHeader(0x28, 0x01, 3), colorWord(10, 20, 30), pair(0, 1), pair(2, 3)},
              {primitiveHeader(0x20, 0x04, 5), colorWord(255, 0, 0), colorWord(0, 255, 0),
               colorWord(0, 0, 255), pair(0, 0), pair(1, 2)},
              {primitiveHeader(0x40, 0x01, 2), colorWord(1, 2, 3), pair(0, 1)},
          },
  };
  return buildTmd({first, second});
}

// Builds an MMD: header, the TMD, then the animation table and the streams. An empty stream leaves
// its entry at 0.
inline std::vector<std::byte> buildMmd(std::span<const std::byte> tmd,
                                       const std::vector<std::vector<std::byte>>& streams) {
  ByteWriter mmd;
  mmd.u32(8);
  mmd.u32(static_cast<std::uint32_t>(8 + tmd.size()));
  mmd.append(tmd);
  const std::size_t table = mmd.size();
  for (std::size_t i = 0; i < streams.size(); ++i) {
    mmd.u32(0);
  }
  for (std::size_t i = 0; i < streams.size(); ++i) {
    if (streams[i].empty()) {
      continue;
    }
    mmd.patchU32(table + (i * 4), static_cast<std::uint32_t>(mmd.size() - table));
    mmd.append(streams[i]);
  }
  return mmd.bytes();
}

// The skeleton of the sample animations: the entity root, the body root, and two bones drawing
// objects 0 and 1.
inline constexpr std::size_t SampleBoneCount = 4;

// Animation with a scale per bone, ten frames: keyframes on frame 1 (bone 1 rotation Y and location
// X, bone 2 scale X, root motion on Z), a sound and a texture move on 2, keyframes that stop
// everything on 4, a loop from 5 to 8 played twice that turns bone 3 on X during frame 6, and a
// last sound on 9. A texture move never starts a frame in the original: the check for a loop start
// at the top of a tick would take it for one.
inline std::vector<std::byte> buildSampleAnimationWithLoop() {
  ByteWriter s;
  s.u16(0x8000 | 10);
  s.words({4096, 4096, 4096, 0, 100, 0, 0, -50, 0});
  s.words({2048, 4096, 8192, 0, 0, 0, 10, 0, 0});
  s.words({4096, 4096, 4096, 1024, 0, 0, 0, 20, 0});
  s.words({0x0001, 0x8000 | 0x400 | 0x100 | 1, 4, 10, -8, 0x8000 | 0x4000 | 2, 2, 4096,
           0x8000 | 0x40 | 0, 1, 10});
  s.words({0x4002, 0x0405});
  s.words({0x3002, 0x0102, 0x0304, 0x0506});
  s.words({0x0004, 0x8000 | 0x400 | 0x100 | 1, 1, 0, 0, 0x8000 | 0x4000 | 2, 1, 0,
           0x8000 | 0x40 | 0, 1, 0});
  s.words({0x1002});
  s.words({0x0006, 0x8000 | 0x800 | 3, 1, 4});
  s.words({0x0007, 0x8000 | 0x800 | 3, 1, 0});
  s.words({0x2008, 5});
  s.words({0x4009, 0x0101});
  s.words({0x0FFF});
  return s.bytes();
}

// Animation without scale whose loop from frame 2 to 4 repeats forever, turning bone 1 on Z.
inline std::vector<std::byte> buildSampleAnimationForever() {
  ByteWriter s;
  s.u16(100);
  s.words({0, 0, 0, 0, 0, 0});
  s.words({0, 0, 0, 0, 0, 0});
  s.words({0, 0, 0, 0, 0, 0});
  s.words({0x10FF, 0x0002, 0x8000 | 0x200 | 1, 1, 3, 0x2004, 2});
  return s.bytes();
}

} // namespace dwe::formats::model::testing
