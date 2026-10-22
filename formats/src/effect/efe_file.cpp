#include <dwe/formats/effect/efe_file.hpp>

#include <bytes/byte_cursor.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::formats::effect {

namespace {

using bytes::ByteCursor;

// BTL_setupLoadedEFEFile, state 0: cell i takes texture page 25 + i / 8 at row (i % 8) * 32, and
// CLUT rows from (128 + (i / 4) * 16, 488 + (i % 4) * 6).
constexpr std::uint16_t FirstCellPage = 0x19;
constexpr std::size_t CellsPerPage = 8;
constexpr std::uint16_t CellRows = 32;
constexpr std::size_t CellsPerClutColumn = 4;
constexpr std::uint16_t FirstClutX = 128;
constexpr std::uint16_t ClutColumnWidth = 16;
constexpr std::uint16_t FirstClutY = 488;
constexpr std::uint16_t ClutRowsPerCell = 6;
constexpr std::uint16_t PageWidth = 64;
constexpr std::uint16_t PageHeight = 256;
constexpr std::uint16_t PagesPerRow = 16;

// TMD layout (include/dw/model.h: TMDModel).
constexpr std::size_t TmdObjectTable = 12;
constexpr std::size_t TmdObjectSize = 28;
constexpr std::size_t TmdPrimitiveTopField = 16;
constexpr std::size_t TmdVertexTopField = 0;
constexpr std::size_t TmdVertexCountField = 4;
constexpr std::size_t TmdNormalTopField = 8;
constexpr std::size_t TmdNormalCountField = 12;
constexpr std::size_t TmdPrimitiveCountField = 20;
constexpr std::size_t TmdVectorSize = 8;
constexpr std::uint32_t TmdTexturedMode = 0x04;
constexpr std::uint32_t TmdQuadMode = 0x08;
constexpr std::uint32_t TsbPageMask = 0xFFE0FFFF;

std::uint16_t clutPage(std::uint16_t x, std::uint16_t y) {
  return static_cast<std::uint16_t>((y << 6U) | (x >> 4U));
}

std::uint32_t loadWord(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= std::to_integer<std::uint32_t>(bytes[offset + i]) << (8U * i);
  }
  return value;
}

void storeWord(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) {
    bytes[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
  }
}

} // namespace

core::Result<std::size_t> efeSlotCount(std::size_t fileSize) {
  if (fileSize == 0 || fileSize % EfeSlotSize != 0) {
    return core::fail(
        std::format("{} bytes are not whole slots of {} bytes", fileSize, EfeSlotSize));
  }
  return fileSize / EfeSlotSize;
}

core::Result<std::span<const std::byte>> efeSlot(std::span<const std::byte> file,
                                                 std::size_t slot) {
  if (slot >= file.size() / EfeSlotSize) {
    return core::fail(
        std::format("slot {} is past the file's {} slots", slot, file.size() / EfeSlotSize));
  }
  return file.subspan(slot * EfeSlotSize, EfeSlotSize);
}

bool isEmptyEfeSlot(std::span<const std::byte> slot) {
  return std::ranges::all_of(slot, [](std::byte b) { return b == EfeFill; });
}

core::Result<EfeHeader> readEfeHeader(std::span<const std::byte> slot) {
  ByteCursor c{slot};
  EfeHeader h{
      .startScript = c.i32(),
      .initScript = c.i32(),
      .uvAnims = c.i32(),
      .uvAnimsEnd = c.i32(),
      .subEffects = c.i32(),
      .tmdStart = c.i32(),
      .tmdEnd = c.i32(),
      .timStart = c.i32(),
      .timEnd = c.i32(),
      .dataEnd = c.i32(),
      .spawnCount = c.i32(),
      .numSubEffects = c.i32(),
      .effectId = c.i32(),
  };
  if (c.overrun()) {
    return core::fail("the slot is shorter than its header");
  }
  const std::array<std::int32_t, 10> order = {h.startScript, h.initScript, h.uvAnims, h.uvAnimsEnd,
                                              h.subEffects,  h.tmdStart,   h.tmdEnd,  h.timStart,
                                              h.timEnd,      h.dataEnd};
  if (order.front() < 0 || !std::ranges::is_sorted(order)) {
    return core::fail("the header's offsets are not in the order of the slot's parts");
  }
  if (static_cast<std::size_t>(h.dataEnd) > slot.size() - EfeHeaderSize) {
    return core::fail(std::format("the data ends at {:#x}, past the slot",
                                  EfeHeaderSize + static_cast<std::size_t>(h.dataEnd)));
  }
  if (h.uvAnimsEnd != h.subEffects || h.tmdEnd != h.timStart || h.timEnd != h.dataEnd) {
    return core::fail("the header's parts leave gaps between them");
  }
  if (static_cast<std::size_t>(h.uvAnimsEnd - h.uvAnims) % EfeUvAnimSize != 0) {
    return core::fail("the UV animation room is not whole EfeUvAnim records");
  }
  if (h.numSubEffects < 0 || static_cast<std::size_t>(h.tmdStart - h.subEffects) !=
                                 static_cast<std::size_t>(h.numSubEffects) * EfeSubEffectSize) {
    return core::fail(
        std::format("the sub-effect room does not hold {} EfeSubEffect records", h.numSubEffects));
  }
  return h;
}

std::span<const std::byte> efePayload(std::span<const std::byte> slot, const EfeHeader& header) {
  return slot.subspan(EfeHeaderSize, static_cast<std::size_t>(header.dataEnd));
}

core::Result<EfeVramCell> efeVramCell(std::size_t cell) {
  if (cell >= EfeVramCellCount) {
    return core::fail(std::format("there is no VRAM cell {}", cell));
  }
  const auto page = static_cast<std::uint16_t>(FirstCellPage + (cell / CellsPerPage));
  return EfeVramCell{
      .pixelPage = page,
      .pixelX = static_cast<std::uint16_t>((page % PagesPerRow) * PageWidth),
      .pixelY =
          static_cast<std::uint16_t>((static_cast<std::size_t>(page / PagesPerRow) * PageHeight) +
                                     ((cell % CellsPerPage) * CellRows)),
      .clutX =
          static_cast<std::uint16_t>(FirstClutX + ((cell / CellsPerClutColumn) * ClutColumnWidth)),
      .clutY =
          static_cast<std::uint16_t>(FirstClutY + ((cell % CellsPerClutColumn) * ClutRowsPerCell)),
  };
}

core::Result<void> placeEfeModel(std::span<std::byte> tmd, const EfeVramCell& cell) {
  const ByteReader reader{tmd};
  const auto objectCount = reader.u32(8);
  if (!objectCount || !reader.contains(TmdObjectTable, std::size_t{*objectCount} * TmdObjectSize)) {
    return core::fail("the TMD is shorter than its object table");
  }
  const EfeVramCell first = *efeVramCell(0);
  // The UVs move by the cell's offset inside its page, the CBA by the distance between the two
  // CLUTs.
  const auto pageOffset =
      static_cast<std::uint32_t>(((cell.pixelY % PageHeight) << 8U) + (cell.pixelX % PageWidth));
  const auto clutDelta = static_cast<std::uint32_t>(clutPage(cell.clutX, cell.clutY) -
                                                    clutPage(first.clutX, first.clutY));
  for (std::uint32_t object = 0; object < *objectCount; ++object) {
    const std::size_t entry = TmdObjectTable + (std::size_t{object} * TmdObjectSize);
    // The object table was bounds-checked above.
    std::size_t primitive = TmdObjectTable + reader.u32(entry + TmdPrimitiveTopField).value_or(0);
    const std::uint32_t count = reader.u32(entry + TmdPrimitiveCountField).value_or(0);
    for (std::uint32_t i = 0; i < count; ++i) {
      const auto header = reader.u32(primitive);
      if (!header) {
        return core::fail(std::format("object {}'s primitive {} is past the TMD", object, i));
      }
      const std::uint32_t mode = *header >> 24U;
      if ((mode & TmdTexturedMode) == 0) {
        break;
      }
      const std::size_t words = ((*header >> 8U) & 0xFFU) + 1;
      const std::size_t uvWords = (mode & TmdQuadMode) != 0 ? 4 : 3;
      if (words <= uvWords || !reader.contains(primitive, words * 4)) {
        return core::fail(std::format("object {}'s primitive {} is past the TMD", object, i));
      }
      storeWord(tmd, primitive + 4, loadWord(tmd, primitive + 4) + (clutDelta << 16U) + pageOffset);
      storeWord(tmd, primitive + 8,
                pageOffset + ((loadWord(tmd, primitive + 8) & TsbPageMask) +
                              (std::uint32_t{cell.pixelPage} << 16U)));
      for (std::size_t word = 3; word <= uvWords; ++word) {
        storeWord(tmd, primitive + (word * 4), loadWord(tmd, primitive + (word * 4)) + pageOffset);
      }
      primitive += words * 4;
    }
  }
  return {};
}

core::Result<std::vector<EfeByteRange>> efeModelGaps(std::span<const std::byte> tmd) {
  const ByteReader reader{tmd};
  const auto objectCount = reader.u32(8);
  if (!objectCount || !reader.contains(TmdObjectTable, std::size_t{*objectCount} * TmdObjectSize)) {
    return core::fail("the TMD is shorter than its object table");
  }
  std::vector<bool> covered(tmd.size(), false);
  const auto cover = [&](std::size_t offset, std::size_t size) {
    std::fill_n(covered.begin() + static_cast<std::ptrdiff_t>(offset), size, true);
  };
  cover(0, TmdObjectTable + (std::size_t{*objectCount} * TmdObjectSize));
  const auto field = [&](std::size_t entry, std::size_t at) {
    return reader.u32(entry + at).value_or(0);
  };
  for (std::uint32_t object = 0; object < *objectCount; ++object) {
    const std::size_t entry = TmdObjectTable + (std::size_t{object} * TmdObjectSize);
    for (const auto [top, count] : {std::pair{TmdVertexTopField, TmdVertexCountField},
                                    std::pair{TmdNormalTopField, TmdNormalCountField}}) {
      const std::size_t start = TmdObjectTable + field(entry, top);
      const std::size_t size = std::size_t{field(entry, count)} * TmdVectorSize;
      if (!reader.contains(start, size)) {
        return core::fail(std::format("object {}'s vertices or normals are past the TMD", object));
      }
      cover(start, size);
    }
    std::size_t primitive = TmdObjectTable + field(entry, TmdPrimitiveTopField);
    for (std::uint32_t i = 0; i < field(entry, TmdPrimitiveCountField); ++i) {
      const auto header = reader.u32(primitive);
      const std::size_t size = header ? (((*header >> 8U) & 0xFFU) + 1) * 4 : 0;
      if (!header || !reader.contains(primitive, size)) {
        return core::fail(std::format("object {}'s primitive {} is past the TMD", object, i));
      }
      cover(primitive, size);
      primitive += size;
    }
  }
  std::vector<EfeByteRange> gaps;
  for (std::size_t at = 0; at < covered.size();) {
    const auto end = static_cast<std::size_t>(
        std::find(covered.begin() + static_cast<std::ptrdiff_t>(at), covered.end(), !covered[at]) -
        covered.begin());
    if (!covered[at]) {
      gaps.push_back(
          {.offset = static_cast<std::uint32_t>(at), .size = static_cast<std::uint32_t>(end - at)});
    }
    at = end;
  }
  return gaps;
}

} // namespace dwe::formats::effect
