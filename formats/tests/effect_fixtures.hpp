#pragma once

#include <dwe/formats/effect/efe_file.hpp>
#include <dwe/formats/effect/efe_script.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "model_fixtures.hpp"
#include "tim_builder.hpp"

// Builders for synthetic effect slots: scripts assembled instruction by instruction and the parts a
// slot holds, laid out as the header says. Nothing comes from the disc.
namespace dwe::formats::effect::testing {

inline constexpr std::uint16_t FillVariant = 0xCD;

// A variable as the scripts address it: an offset and the flag that says from where.
struct Var {
  std::int16_t offset = 0;
  bool base = true;
};

inline Var base(std::int16_t offset) {
  return {.offset = offset, .base = true};
}

inline Var instance(std::int16_t offset) {
  return {.offset = offset, .base = false};
}

// Assembles a script into halfwords. Offsets are from the slot's base, so the writer starts where
// the script will.
class ScriptWriter {
public:
  explicit ScriptWriter(std::uint32_t start) : start_(start) {}

  [[nodiscard]] std::uint32_t here() const {
    return start_ + static_cast<std::uint32_t>(words_.size() * 2);
  }
  [[nodiscard]] const std::vector<std::uint16_t>& words() const { return words_; }

  void loadImmediate(std::int32_t value) { immediate(0x00, value); }
  void pushImmediate(std::int32_t value) { immediate(0x0A, value); }
  void loadVariable(Var var, unsigned width) { variable(0x01, width, var); }
  void pushVariable(Var var, unsigned width) { variable(0x0B, width, var); }
  void pushAddress(Var var, unsigned width = 4) { variable(0x0C, width, var); }
  void popToVariable(Var var) { variable(0x10, FillVariant, var); }
  void loadRandom() { op(0x02, FillVariant); }
  void loadIndexed(std::int16_t table, unsigned width, Var index, unsigned indexWidth) {
    op(0x03, indexWidth << 4U | width, {static_cast<std::uint16_t>(table), EfeBaseFlag});
    put(index);
  }
  void applyOperator(unsigned row, unsigned operation, Var var) {
    variable(0x04, operation << 4U | row, var);
  }
  void branchIf(unsigned comparison, unsigned width, Var var, std::uint32_t target) {
    variable(0x07, comparison << 4U | width, var);
    words_.push_back(static_cast<std::uint16_t>(target));
    words_.push_back(EfeBaseFlag);
  }
  void jump(std::uint32_t target) {
    op(0x09, FillVariant, {static_cast<std::uint16_t>(target), EfeBaseFlag});
  }
  void call(std::uint32_t target) {
    op(0x0D, FillVariant, {static_cast<std::uint16_t>(target), EfeBaseFlag});
  }
  void sub(unsigned code) { op(0x0E, code); }
  void ret() { op(0x0F, FillVariant); }
  void stop() { op(0x05, FillVariant); }
  void spawn(unsigned ordinal, std::int16_t count, std::int16_t table, std::int16_t stride) {
    op(0x11, ordinal,
       {static_cast<std::uint16_t>(count), static_cast<std::uint16_t>(table), EfeBaseFlag,
        static_cast<std::uint16_t>(stride), 0});
  }
  // Leaves a target to fill in later: returns the index of its halfword.
  [[nodiscard]] std::size_t lastWord() const { return words_.size() - 1; }
  void patch(std::size_t word, std::uint32_t value) {
    words_[word] = static_cast<std::uint16_t>(value);
  }
  void raw(std::uint16_t word) { words_.push_back(word); }

private:
  void op(unsigned code, unsigned variant, std::initializer_list<std::uint16_t> operands = {}) {
    words_.push_back(static_cast<std::uint16_t>(variant << 8U | code));
    words_.insert(words_.end(), operands);
  }
  void put(Var var) {
    words_.push_back(static_cast<std::uint16_t>(var.offset));
    words_.push_back(var.base ? EfeBaseFlag : 0);
  }
  void immediate(unsigned code, std::int32_t value) {
    op(code, FillVariant,
       {static_cast<std::uint16_t>(value & 0xFFFF),
        static_cast<std::uint16_t>((value >> 16) & 0xFFFF)});
  }
  void variable(unsigned code, unsigned variant, Var var) {
    op(code, variant);
    put(var);
  }

  std::uint32_t start_;
  std::vector<std::uint16_t> words_;
};

// What goes in a slot, in the order the original lays it out.
struct SlotParts {
  std::vector<std::byte> data;
  std::vector<std::uint16_t> startScript;
  std::vector<std::uint16_t> initScript;
  std::size_t subEffectCount = 0;
  std::vector<std::byte> tmd; // the UV animation room gets one record per object
  std::vector<std::byte> tim;
  std::int32_t spawnCount = 0;
  std::int32_t effectId = 0;
};

// The number of objects of a TMD, from its header.
inline std::size_t tmdObjectCount(const std::vector<std::byte>& tmd) {
  if (tmd.size() < 12) {
    return 0;
  }
  return std::to_integer<std::size_t>(tmd[8]) | std::to_integer<std::size_t>(tmd[9]) << 8U;
}

// Builds a whole slot: header, parts, rooms of fill and fill to the slot's end.
inline std::vector<std::byte> buildSlot(const SlotParts& parts) {
  const auto start = static_cast<std::uint32_t>(parts.data.size());
  const auto init = start + static_cast<std::uint32_t>(parts.startScript.size() * 2);
  const auto uvAnims = init + static_cast<std::uint32_t>(parts.initScript.size() * 2);
  const auto subEffects =
      uvAnims + static_cast<std::uint32_t>(tmdObjectCount(parts.tmd) * EfeUvAnimSize);
  const auto tmdStart =
      subEffects + static_cast<std::uint32_t>(parts.subEffectCount * EfeSubEffectSize);
  const auto timStart = tmdStart + static_cast<std::uint32_t>(parts.tmd.size());
  const auto dataEnd = timStart + static_cast<std::uint32_t>(parts.tim.size());

  model::testing::ByteWriter out;
  for (const std::uint32_t field :
       {start, init, uvAnims, subEffects, subEffects, tmdStart, timStart, timStart, dataEnd,
        dataEnd, static_cast<std::uint32_t>(parts.spawnCount),
        static_cast<std::uint32_t>(parts.subEffectCount),
        static_cast<std::uint32_t>(parts.effectId)}) {
    out.u32(field);
  }
  out.append(parts.data);
  for (const std::uint16_t word : parts.startScript) {
    out.u16(word);
  }
  for (const std::uint16_t word : parts.initScript) {
    out.u16(word);
  }
  while (out.size() < EfeHeaderSize + tmdStart) {
    out.u8(0xCD);
  }
  out.append(parts.tmd);
  out.append(parts.tim);
  while (out.size() < EfeSlotSize) {
    out.u8(0xCD);
  }
  return out.bytes();
}

// A 16 x 4 4-bit TIM with one palette row, uploaded somewhere the original ignores.
inline std::vector<std::byte> sampleEffectTim() {
  std::vector<image::Color15> clut(16);
  for (std::size_t i = 0; i < clut.size(); ++i) {
    clut[i] = static_cast<image::Color15>(i * 0x0421U);
  }
  std::vector<std::byte> pixels(std::size_t{4} * 2 * 4);
  for (std::size_t i = 0; i < pixels.size(); ++i) {
    pixels[i] = static_cast<std::byte>(i * 0x11U);
  }
  return image::test::buildTim(image::test::indexedTim(
      image::TimPixelMode::Indexed4, {.x = 0, .y = 480, .width = 16, .height = 1}, clut,
      {.x = 320, .y = 0, .width = 4, .height = 4}, pixels));
}

// One textured triangle drawn with cell 0's CLUT, which is what the effect TMDs are made for.
inline std::vector<std::byte> sampleEffectTmd() {
  using namespace model::testing;
  const TestObject triangle{
      .vertices = {0, 0, 0, 100, 0, 0, 0, -100, 0},
      .normals = {0, 0, -4096, 0, -4096, 0},
      .primitives = {{primitiveHeader(0x34, 0, 6), uvWord(0, 0, 0x7A08), uvWord(15, 0, 0x0005),
                      uvWord(0, 3, 0), pair(0, 0), pair(0, 1), pair(1, 2)}},
  };
  return buildTmd({triangle});
}

// Where everything of the sample slot's data area is: one item of each kind a sub-op argument
// points at.
namespace layout {
inline constexpr std::int16_t Instances = 0x00;   // 1 instance of 8 bytes
inline constexpr std::int16_t Counter = 0x08;     // int32
inline constexpr std::int16_t Table = 0x0C;       // 3 int16
inline constexpr std::int16_t Index = 0x12;       // int16
inline constexpr std::int16_t Scale = 0x14;       // vector
inline constexpr std::int16_t Tint = 0x20;        // color
inline constexpr std::int16_t Target = 0x2C;      // int32
inline constexpr std::int16_t Bones = 0x30;       // 1 entry and the end
inline constexpr std::int16_t Keyframes = 0x3C;   // count 1 and 1 keyframe
inline constexpr std::int16_t Parent = 0x44;      // 4 bytes
inline constexpr std::int16_t Radii = 0x48;       // 2 int32
inline constexpr std::int16_t Heights = 0x50;     // 2 int32
inline constexpr std::int16_t Colors = 0x58;      // 2 int32
inline constexpr std::int16_t Clut = 0x60;        // 1 colour of 4 uint16
inline constexpr std::int16_t ClutRow = 0x68;     // 16 colours
inline constexpr std::int16_t Tiles = 0x88;       // 11 x 9 tile ids, the highest 1
inline constexpr std::int16_t TileOffsets = 0xEB; // 2 int16
inline constexpr std::int16_t Sprites = 0xEF;     // 1 entry and the end
inline constexpr std::int16_t Ribbon = 0xFB;      // 10 points
inline constexpr std::int16_t Corners = 0x14B;    // 4 corners of 8 bytes
inline constexpr std::int16_t Fill = 0x16B;       // 1 fill byte
inline constexpr std::int16_t End = 0x16C;
} // namespace layout

inline std::vector<std::byte> sampleData() {
  model::testing::ByteWriter data;
  const auto fillTo = [&](std::int16_t offset) {
    while (data.size() < static_cast<std::size_t>(offset)) {
      data.u8(0xCD);
    }
  };
  data.u32(0xFFFFFFFF);
  fillTo(layout::Counter);
  data.u32(7);
  data.words({1, 2, 3, 1});
  data.u32(100); // scale
  data.u32(200);
  data.u32(300);
  data.u32(255); // tint
  data.u32(128);
  data.u32(0);
  fillTo(layout::Bones);
  data.words({3, 1, 10, 20, 30, -1});
  data.u32(1); // keyframes
  data.words({8});
  data.u8(2);
  data.u8(0xFF);
  data.u32(0x01020304); // parent field
  for (std::uint32_t i = 0; i < 6; ++i) {
    data.u32(i);
  }
  data.words({31, 0, 0, 1});
  for (std::uint16_t i = 0; i < 16; ++i) {
    data.u16(static_cast<std::uint16_t>(0x8000U + i));
  }
  for (std::size_t i = 0; i < 99; ++i) {
    data.u8(i == 50 ? 1 : 0);
  }
  data.words({0, 16});
  data.words({64, 1, 2, 3, 4, -1});
  for (std::int32_t i = 0; i < 40; ++i) {
    data.s16(i);
  }
  for (std::int32_t i = 0; i < 16; ++i) {
    data.s16(i * 10);
  }
  data.u8(0xCD);
  return data.bytes();
}

// A start script that runs every opcode and gives every kind of sub-op argument what it expects,
// then the labels it jumps to.
inline std::vector<std::uint16_t> sampleStartScript(std::uint32_t start) {
  using namespace layout;
  ScriptWriter w{start};
  w.spawn(0, 1, Instances, 8);
  w.pushAddress(base(0));
  const std::size_t tick = w.lastWord() - 1;
  w.pushAddress(base(0));
  const std::size_t render = w.lastWord() - 1;
  w.sub(2);
  w.loadImmediate(5);
  w.applyOperator(4, 0, base(Counter));
  w.loadVariable(base(Counter), 4);
  w.loadRandom();
  w.loadIndexed(Table, 2, base(Index), 2);
  w.branchIf(0, 4, base(Counter), 0);
  const std::size_t branch = w.lastWord() - 1;
  w.jump(0);
  const std::size_t jump = w.lastWord() - 1;
  w.patch(branch, w.here());
  w.patch(jump, w.here());
  w.pushVariable(base(Counter), 4);
  w.popToVariable(instance(4));
  w.call(0);
  const std::size_t call = w.lastWord() - 1;

  w.pushImmediate(0);
  w.pushAddress(base(Scale));
  w.sub(3);
  w.pushImmediate(0);
  w.pushAddress(base(Tint));
  w.sub(15);
  w.pushAddress(base(Target));
  w.sub(17);
  w.pushAddress(base(Bones));
  w.sub(0);
  w.pushImmediate(0);
  w.pushImmediate(9);
  w.pushAddress(base(Keyframes));
  w.sub(1);
  w.pushAddress(base(Parent));
  w.pushImmediate(0x10);
  w.pushImmediate(4);
  w.sub(26);
  w.pushAddress(base(Scale));
  w.pushImmediate(2);
  w.pushAddress(base(Radii));
  w.pushAddress(base(Heights));
  w.pushAddress(base(Colors));
  w.pushImmediate(0);
  w.sub(86);
  w.pushImmediate(0);
  w.pushImmediate(0);
  w.pushImmediate(1);
  w.pushAddress(base(Clut));
  w.sub(78);
  w.pushImmediate(0);
  w.pushImmediate(1);
  w.pushAddress(base(ClutRow));
  w.pushImmediate(0);
  w.sub(80);
  w.pushAddress(base(Tiles));
  w.pushAddress(base(TileOffsets));
  w.pushImmediate(0);
  w.pushImmediate(0);
  w.pushImmediate(128);
  w.sub(30);
  w.pushAddress(base(Sprites));
  w.pushImmediate(0);
  w.pushImmediate(0);
  w.sub(31);
  w.pushAddress(base(Ribbon));
  w.sub(83);
  for (std::int16_t corner = 0; corner < 4; ++corner) {
    w.pushAddress(base(static_cast<std::int16_t>(Corners + (corner * 8))));
  }
  w.pushImmediate(0);
  w.pushAddress(base(Tint));
  for (int i = 0; i < 5; ++i) {
    w.pushImmediate(0);
  }
  w.pushImmediate(-1);
  w.sub(65);
  w.pushImmediate(1);
  w.pushImmediate(2);
  w.sub(11);
  w.stop();

  w.patch(call, w.here());
  w.ret();
  w.patch(tick, w.here());
  w.ret();
  w.patch(render, w.here());
  w.ret();
  return w.words();
}

// The sample slot: the data and start script above, an init script that returns, the sample TMD and
// a 4-bit TIM.
inline SlotParts sampleSlotParts() {
  SlotParts parts;
  parts.data = sampleData();
  parts.startScript = sampleStartScript(static_cast<std::uint32_t>(parts.data.size()));
  ScriptWriter init{static_cast<std::uint32_t>(parts.data.size() + (parts.startScript.size() * 2))};
  init.ret();
  parts.initScript = init.words();
  parts.subEffectCount = 1;
  parts.tmd = sampleEffectTmd();
  parts.tim = sampleEffectTim();
  parts.spawnCount = 1;
  parts.effectId = static_cast<std::int32_t>(EfeEffectIdBase);
  return parts;
}

} // namespace dwe::formats::effect::testing
