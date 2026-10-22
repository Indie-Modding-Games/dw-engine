#pragma once

#include <dwe/core/error.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// The VAB of the console's sound library (libsnd): a VH (header, programs, tones and the sizes of
// the samples) and a VB (the samples in ADPCM, one after another). Field names follow libsnd's
// VabHdr, ProgAtr and VagAtr; the game loads the VH with SsVabOpenHeadSticky and the VB with
// SsVabTransBody (sound.c: readVHBFile).
namespace dwe::formats::audio {

inline constexpr std::size_t VabProgramSlots = 128;
inline constexpr std::size_t VabTonesPerProgram = 16;
inline constexpr std::size_t VabVagSlots = 256;

// VabHdr: the first 0x20 bytes of a VH.
struct VabHeader {
  std::uint32_t version = 0; // 7 in every bank of the game
  std::uint32_t id = 0;
  std::uint32_t fsize = 0; // VH and VB together
  std::uint16_t reserved0 = 0;
  std::uint16_t ps = 0; // programs with tones
  std::uint16_t ts = 0; // tones
  std::uint16_t vs = 0; // samples (VAGs)
  std::uint8_t mvol = 0;
  std::uint8_t pan = 0;
  std::uint8_t attr1 = 0;
  std::uint8_t attr2 = 0;
  std::uint32_t reserved1 = 0;

  bool operator==(const VabHeader&) const = default;
};

// ProgAtr: one of the 128 program slots.
struct ProgramAttributes {
  std::uint8_t tones = 0; // 0 = empty slot
  std::uint8_t mvol = 0;
  std::uint8_t prior = 0;
  std::uint8_t mode = 0;
  std::uint8_t mpan = 0;
  std::uint8_t reserved0 = 0;
  std::int16_t attr = 0;
  std::uint32_t reserved1 = 0;
  std::uint32_t reserved2 = 0;

  bool operator==(const ProgramAttributes&) const = default;
};

// VagAtr: a tone, the sample a program plays over a key range.
struct ToneAttributes {
  std::uint8_t prior = 0;
  std::uint8_t mode = 0; // bit 2: the voice goes to the reverb
  std::uint8_t vol = 0;
  std::uint8_t pan = 0;    // 64 = centre
  std::uint8_t center = 0; // the key the sample plays at its own pitch
  std::uint8_t shift = 0;  // the centre's fine tune, in 128ths of a semitone
  std::uint8_t min = 0;    // key range
  std::uint8_t max = 0;
  std::uint8_t vibW = 0;
  std::uint8_t vibT = 0;
  std::uint8_t porW = 0;
  std::uint8_t porT = 0;
  std::uint8_t pbmin = 0; // pitch bend range down and up, in semitones
  std::uint8_t pbmax = 0;
  std::uint8_t reserved1 = 0;
  std::uint8_t reserved2 = 0;
  std::uint16_t adsr1 = 0; // the sound chip's envelope registers, as they are
  std::uint16_t adsr2 = 0;
  std::int16_t prog = 0; // the program the tone belongs to
  std::int16_t vag = 0;  // 1-based sample number
  std::array<std::int16_t, 4> reserved{};

  static constexpr std::uint8_t ReverbMode = 0x04;

  bool operator==(const ToneAttributes&) const = default;
};

// The tones of one program. The VH stores a block of 16 for each program with tones, in program
// order.
struct ToneBlock {
  std::size_t program = 0;
  std::array<ToneAttributes, VabTonesPerProgram> tones{};
};

// A VH.
struct Vab {
  VabHeader header;
  std::array<ProgramAttributes, VabProgramSlots> programs{};
  std::vector<ToneBlock> toneBlocks;
  std::array<std::uint16_t, VabVagSlots> vagSizes{}; // size of each sample >> 3; entry 0 is unused

  // The size of the VH in bytes.
  [[nodiscard]] std::size_t size() const;
  // The size of the VB, as SsVabTransBody uploads it: fsize minus the VH.
  [[nodiscard]] std::size_t vbSize() const { return header.fsize - size(); }
  // The tones of a program, or nothing for an empty slot.
  [[nodiscard]] std::span<const ToneAttributes> tonesOf(std::size_t program) const;
};

// Returns the size of a VH with ps programs: 0x20 + 128 * 16 + ps * 16 * 32 + 256 * 2.
std::size_t vabHeaderSize(std::size_t programs);

// Reads a VH. Block k belongs to the k-th program with tones (not to program k: two music banks
// have gaps in their program numbers), so the number of such programs must be ps. Every used tone
// must name the program it is in and a sample of the size table, and fsize must cover the VH.
core::Result<Vab> readVab(std::span<const std::byte> bytes);

// Where a sample lies in the VB.
struct VagLocation {
  std::size_t number = 0; // 1-based, as tones name it
  std::size_t offset = 0;
  std::size_t size = 0;
  bool beyondCount = false; // after vs: the size table lists it but the header does not count it
};

// Splits the VB into its samples: sample i starts after samples 1..i-1. The vs samples must fit in
// the VB; samples the size table lists after them are returned too, marked, while they fit.
// Zero-size entries are skipped.
core::Result<std::vector<VagLocation>> locateVags(const Vab& vab, std::size_t vbSize);

} // namespace dwe::formats::audio
