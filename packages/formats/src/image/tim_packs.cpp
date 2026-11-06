#include <dwe/formats/byte_reader.hpp>
#include <dwe/formats/disc/sector.hpp>
#include <dwe/formats/image/tim_packs.hpp>

#include <array>
#include <format>
#include <utility>

namespace dwe::formats::image {

namespace {

// Both packs hold one TIM at the start of each entry, padded to the stride; the original reads the
// entry's sectors and parses the TIM in place (GsGetTimInfo on the word after the magic). Their
// sizes in the file table of every release are exactly entryCount × stride.
constexpr std::array KnownPacks = {
    // The TIM's own video memory position is ignored: uploadModelTexture places it at the model's
    // texture page and
    // CLUT. 180 is the number of Digimon types (loadMMDAsync rejects digimonType >= 0xB4).
    KnownPack{
        .path = "CHDAT\\ALLTIM.TIM",
        .strideBytes = 9 * disc::Form1DataSize,
        .entryCount = 180,
        .entryFormat = PackEntryFormat::Tim,
        .indexedBy = "Digimon type",
        .source =
            "model.c: loadDigimonTexture, loadMMDAsync (readFileSectors(type * 9, 9)), uploadModelTexture"},
    // The TIM is uploaded at its own position. 66 is the number of cards (CARD_DATA, 0x108 bytes of
    // 4-byte entries).
    KnownPack{
        .path = "CARD\\CARD.ALL",
        .strideBytes = 14 * disc::Form1DataSize,
        .entryCount = 66,
        .entryFormat = PackEntryFormat::Tim,
        .indexedBy = "card id",
        .source =
            "game_menu.c: loadCardImage (loadStackedTIMEntry(id * 0xe, 0xe)); file.c: loadStackedTIMEntry"},
};

// The bytes the original skips for one stacked TIM, magic included, computed as loadStackedTIMFile
// does. The halving of width × height rounds down, as in the original.
std::size_t originalStackedSize(const Tim& tim) {
  const std::size_t imageUnits = std::size_t{tim.imageRect.width} * tim.imageRect.height;
  std::size_t size = 4 + (((imageUnits / 2) + 4) * 4);
  if (tim.hasClut) {
    const std::size_t clutUnits = std::size_t{tim.clutRect.width} * tim.clutRect.height;
    size += ((clutUnits / 2) + 3) * 4;
  }
  return size;
}

// How far one TIM of a list is from the next.
enum class Stride : std::uint8_t {
  Original,     // what width × height give, as loadStackedTIMFile computes it
  BlockLengths, // what the block lengths say
};

core::Result<StackedTims> walkTims(std::span<const std::byte> bytes, Stride stride) {
  StackedTims stack;
  const ByteReader reader{bytes};
  std::size_t offset = 0;
  while (reader.u32(offset) == TimMagic) {
    auto tim = readTim(bytes.subspan(offset));
    if (!tim) {
      return core::fail(
          std::format("TIM {} at 0x{:x}: {}", stack.entries.size(), offset, tim.error().message));
    }
    std::size_t size = tim->byteSize;
    if (stride == Stride::Original) {
      size = originalStackedSize(*tim);
      if (size != tim->byteSize) {
        stack.warnings.push_back(
            std::format("TIM {} at 0x{:x}: width × height give {} bytes, the block "
                        "lengths give {}; using {}",
                        stack.entries.size(), offset, size, tim->byteSize, size));
      }
    }
    if (!reader.contains(offset, size)) {
      return core::fail(
          std::format("TIM {} at 0x{:x} runs past the end", stack.entries.size(), offset));
    }
    stack.entries.push_back(StackedTim{.offset = offset, .tim = std::move(*tim)});
    offset += size;
  }
  stack.end = offset;
  if (stack.entries.empty()) {
    stack.warnings.emplace_back("the file does not start with a TIM");
  }
  return stack;
}

} // namespace

core::Result<StackedTims> readStackedTims(std::span<const std::byte> bytes) {
  return walkTims(bytes, Stride::Original);
}

core::Result<StackedTims> readTimSequence(std::span<const std::byte> bytes) {
  return walkTims(bytes, Stride::BlockLengths);
}

std::span<const KnownPack> knownPacks() {
  return KnownPacks;
}

core::Result<std::vector<std::span<const std::byte>>> splitPack(std::span<const std::byte> bytes,
                                                                std::size_t strideBytes) {
  if (strideBytes == 0) {
    return core::fail("a pack stride must not be 0");
  }
  if (bytes.size() % strideBytes != 0) {
    return core::fail(std::format("a pack of {} bytes is not a whole number of {}-byte entries",
                                  bytes.size(), strideBytes));
  }
  std::vector<std::span<const std::byte>> entries;
  entries.reserve(bytes.size() / strideBytes);
  for (std::size_t offset = 0; offset < bytes.size(); offset += strideBytes) {
    entries.push_back(bytes.subspan(offset, strideBytes));
  }
  return entries;
}

core::Result<std::vector<std::span<const std::byte>>>
splitKnownPack(std::span<const std::byte> bytes, const KnownPack& pack) {
  auto entries = splitPack(bytes, pack.strideBytes);
  if (!entries) {
    return core::fail(std::format("{}: {}", pack.path, entries.error().message));
  }
  if (entries->size() != pack.entryCount) {
    return core::fail(std::format("{} has {} entries; the original expects {}", pack.path,
                                  entries->size(), pack.entryCount));
  }
  return entries;
}

} // namespace dwe::formats::image
