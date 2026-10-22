#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/exe/memory_image.hpp>

#include <cstdint>
#include <span>
#include <vector>

// Some per-item numbers of the original are not in a data table but in the code: a function
// switches on the item and each case sets a few locals to constants before the cases join and apply
// them (item.c: handleFood, handleChips). This runs such a function from its entry for one
// argument, on the bytes of the user's executable, and reads the locals where the cases join. Only
// the instructions such cases use are known; a path that does anything else (calls a function,
// reads memory other than the stack and jump tables, branches on an unknown value) is reported,
// never guessed.
namespace dwe::formats::exe {

// The general-purpose registers the layouts name (MIPS o32 numbers).
enum class MipsRegister : std::uint8_t {
  Zero = 0,
  V0 = 2,
  V1 = 3,
  A0 = 4,
  S0 = 16,
  S1 = 17,
  S2 = 18,
  S3 = 19,
  S4 = 20,
  S5 = 21,
  S6 = 22,
  S7 = 23,
  Sp = 29,
  S8 = 30,
};

// Where a local is when the cases join: a register, or a word of the stack frame at an offset from
// the stack pointer.
struct ValueLocation {
  enum class Kind : std::uint8_t { Register, Stack };

  Kind kind = Kind::Register;
  MipsRegister reg = MipsRegister::Zero;
  std::int16_t stackOffset = 0;

  static constexpr ValueLocation inRegister(MipsRegister reg) {
    return {.kind = Kind::Register, .reg = reg};
  }
  static constexpr ValueLocation onStack(std::int16_t offset) {
    return {.kind = Kind::Stack, .stackOffset = offset};
  }
};

// A function of the executable whose switch only sets locals: where it starts, where its cases join
// and where each local is at the join, in the order its reader names them. Addresses come from the
// decomp's symbols and from the compiled code; the values are read from the user's executable.
struct ConstantSwitch {
  std::uint32_t function = 0;
  std::uint32_t join = 0;
  std::span<const ValueLocation> values;

  [[nodiscard]] bool located() const { return function != 0 && join != 0 && !values.empty(); }
};

// Runs the switch from its entry with `argument` in a0 and returns the value of each local at the
// join, as signed 32-bit numbers. Fails if the path for that argument does anything but set values.
core::Result<std::vector<std::int32_t>> evaluateConstantSwitch(const MemoryImage& image,
                                                               const ConstantSwitch& code,
                                                               std::uint32_t argument);

} // namespace dwe::formats::exe
