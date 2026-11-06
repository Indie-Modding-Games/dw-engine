#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// ETCHI/EFEDAT.EFE: the effects of the techniques, one fixed-size slot per technique (efe.c:
// getEFEDATEntry, battle_effect.c: BTL_loadNextEFEFile and BTL_setupLoadedEFEFile).
namespace dwe::formats::effect {

// Each slot is read whole: 10 sectors.
inline constexpr std::size_t EfeSlotSize = 0x5000;
inline constexpr std::size_t EfeHeaderSize = 0x34;
// A slot's effect id is the technique id plus this (std_main.c builds it from the Digimon's move).
inline constexpr std::int32_t EfeEffectIdBase = 0x100;
// The byte the authoring tool filled unused memory with; the files keep it in padding and in
// variables that start without a value.
inline constexpr std::byte EfeFill{0xCD};

// EfeFileHeader (include/dw/efe.h). Offsets count from the end of the header, which the original
// calls the slot's base (EFE_SCRIPT_HEAD): the scripts address everything from there.
struct EfeHeader {
  std::int32_t startScript = 0; // the script the effect starts with (BTL_runEFESlotScript)
  std::int32_t initScript = 0;  // the script run while the slot loads, four instructions per frame
  std::int32_t uvAnims = 0; // EfeUvAnim[TMD objects], 0x20 bytes each: room for the UV animations
  std::int32_t uvAnimsEnd = 0;
  std::int32_t subEffects =
      0; // EfeSubEffect[numSubEffects], 0x1C bytes each: room for the running sub-effects
  std::int32_t tmdStart = 0;
  std::int32_t tmdEnd = 0;
  std::int32_t timStart = 0;
  std::int32_t timEnd = 0;
  std::int32_t dataEnd = 0;
  // unk28 in the decomp, which nothing reads: on the disc it always equals the number of spawn
  // instructions.
  std::int32_t spawnCount = 0;
  std::int32_t numSubEffects = 0;
  std::int32_t effectId = 0;

  bool operator==(const EfeHeader&) const = default;
};

inline constexpr std::size_t EfeUvAnimSize = 0x20;
inline constexpr std::size_t EfeSubEffectSize = 0x1C;

// Returns how many slots the file holds; its size must be a whole number of slots.
core::Result<std::size_t> efeSlotCount(std::size_t fileSize);

// Returns the bytes of a slot.
core::Result<std::span<const std::byte>> efeSlot(std::span<const std::byte> file, std::size_t slot);

// Returns true when every byte of the slot is the fill byte: a slot no technique has.
bool isEmptyEfeSlot(std::span<const std::byte> slot);

// Reads a slot's header and checks that its parts follow one another inside the slot, in the order
// the original lays them out: scripts and their data, UV animation room, sub-effect room, TMD, TIM.
core::Result<EfeHeader> readEfeHeader(std::span<const std::byte> slot);

// Returns the slot's bytes from its base (the end of the header) to dataEnd: what the header's
// offsets address.
std::span<const std::byte> efePayload(std::span<const std::byte> slot, const EfeHeader& header);

// Where the original puts a slot's TIM and palettes in VRAM, and how it rewrites the slot's TMD to
// match (BTL_setupLoadedEFEFile, graphics.c: updateTMDTextureData). It loads up to 16 effects at
// once, each in its own cell; the file's own TIM positions are ignored.
struct EfeVramCell {
  std::uint16_t pixelPage =
      0;                    // the texture page of the pixels: x / 64 in bits 0-3, y / 256 in bit 4
  std::uint16_t pixelX = 0; // the pixels' corner, in halfwords
  std::uint16_t pixelY = 0;
  std::uint16_t clutX = 0; // the first CLUT row's corner, in halfwords
  std::uint16_t clutY = 0;
};

inline constexpr std::size_t EfeVramCellCount = 16;

// Returns the place of cell 0 to 15.
core::Result<EfeVramCell> efeVramCell(std::size_t cell);

// Rewrites a TMD as the original does when it loads it into a cell: every textured primitive gets
// the cell's texture page, its UVs move by the cell's offset in the page and its CBA moves from
// cell 0's CLUT to the cell's. Like the original, an object's walk stops at its first untextured
// primitive. The TMD's offsets must be relative to its object table.
core::Result<void> placeEfeModel(std::span<std::byte> tmd, const EfeVramCell& cell);

// A run of bytes: offset and size.
struct EfeByteRange {
  std::uint32_t offset = 0;
  std::uint32_t size = 0;

  bool operator==(const EfeByteRange&) const = default;
};

// Returns the bytes of a TMD that none of its parts covers: its header, object table, primitive
// packets, vertices and normals. Empty when the parts fill it.
core::Result<std::vector<EfeByteRange>> efeModelGaps(std::span<const std::byte> tmd);

} // namespace dwe::formats::effect
