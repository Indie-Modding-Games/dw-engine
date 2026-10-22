#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/audio/vab.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// The game's sound files (SOUND\VHB\*.VHB): a VAB behind a small header, alone or packed many to a
// file.
namespace dwe::formats::audio {

// A VHB: a header of u32 offsets, the VH and the VB. A sound bank has three offsets {vh = 12, vb,
// vbEnd}; a music font (an entry of FAALL) has four {vh = 16, vb, seq, seqEnd} and its SEQs after
// the VB (sound.c: readVHBFile, loadMusicFont).
struct Vhb {
  std::uint32_t vhOffset = 0;
  std::uint32_t vbOffset = 0;
  std::uint32_t end = 0; // where the VHB's data ends: vbEnd, or seqEnd for a music font
  bool musicFont = false;
  Vab vab;
  std::span<const std::byte> vb;
  std::span<const std::byte> seqBank; // empty for a sound bank
};

// Reads a VHB. The VH must take exactly the bytes from vh to vb, and the VB must take fsize minus
// the VH; a sound bank's vbEnd and a music font's seq must be where the VB ends.
core::Result<Vhb> readVhb(std::span<const std::byte> bytes);

inline constexpr std::size_t SoundSectorSize = 2048;

// How a sound file holds its banks.
enum class SoundFileLayout : std::uint8_t {
  Single,      // one bank, read whole (readVHBFile)
  FixedStride, // entryCount entries of entrySectors sectors (readVHBFileSectors)
  MapSlices,   // the slices of MAP_SOUND_PARA (loadMapSounds)
};

// A sound file of the game and how the original loads it. The vab slots are the original's
// (VHB_HEADER_ADDR).
struct SoundFile {
  std::string_view path; // on the volume
  std::string_view name; // the bank's asset name
  SoundFileLayout layout = SoundFileLayout::Single;
  std::uint32_t entrySectors = 0;
  std::uint32_t entryCount = 0;
  std::uint8_t firstVabSlot = 0;
  std::uint8_t lastVabSlot = 0;
  std::string_view loader; // the decomp function that loads it
};

// Returns every sound file of the game.
std::span<const SoundFile> soundFiles();

// Returns the sound file at a volume path, or nullptr.
const SoundFile* findSoundFile(std::string_view path);

// A run of sectors of a packed file.
struct SectorRange {
  std::uint32_t first = 0;
  std::uint32_t count = 0;

  bool operator==(const SectorRange&) const = default;
};

// Returns the entries of a fixed-stride file.
std::vector<SectorRange> fixedStrideEntries(const SoundFile& file);

// Returns the bytes of each entry of a packed file. Every entry must lie inside the file.
core::Result<std::vector<std::span<const std::byte>>>
splitSoundFile(std::span<const std::byte> bytes, std::span<const SectorRange> entries);

} // namespace dwe::formats::audio
