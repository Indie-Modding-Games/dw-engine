#include <dwe/formats/audio/vhb.hpp>
#include <dwe/formats/byte_reader.hpp>

#include <array>
#include <format>

namespace dwe::formats::audio {

namespace {

constexpr std::uint32_t BankHeaderSize = 12;
constexpr std::uint32_t FontHeaderSize = 16;

// The sound files, from sound.c and sound_async.c. ESALL's 22 slices come from the executable's
// MAP_SOUND_PARA.
constexpr SoundFile singleBank(std::string_view path, std::string_view name, std::uint8_t vabSlot,
                               std::string_view loader) {
  return SoundFile{.path = path,
                   .name = name,
                   .layout = SoundFileLayout::Single,
                   .entrySectors = 0,
                   .entryCount = 0,
                   .firstVabSlot = vabSlot,
                   .lastVabSlot = vabSlot,
                   .loader = loader};
}

constexpr std::array SoundFiles = {
    singleBank("SOUND/VHB/SS.VHB", "ss", 0, "sound.c: initializeMusic"),
    singleBank("SOUND/VHB/SL.VHB", "sl", 1, "sound.c: initializeMusic"),
    singleBank("SOUND/VHB/SB.VHB", "sb", 8, "sound.c: VSLoadSounds"),
    SoundFile{.path = "SOUND/VHB/FAALL.VHB",
              .name = "faall",
              .layout = SoundFileLayout::FixedStride,
              .entrySectors = 0x27,
              .entryCount = 33,
              .firstVabSlot = 2,
              .lastVabSlot = 2,
              .loader = "sound.c: loadMusicFont"},
    SoundFile{.path = "SOUND/VHB/VLALL.VHB",
              .name = "vlall",
              .layout = SoundFileLayout::FixedStride,
              .entrySectors = 15,
              .entryCount = 16,
              .firstVabSlot = 3,
              .lastVabSlot = 3,
              .loader = "sound.c: loadPartnerSounds"},
    SoundFile{.path = "SOUND/VHB/VBALL.VHB",
              .name = "vball",
              .layout = SoundFileLayout::FixedStride,
              .entrySectors = 7,
              .entryCount = 71,
              .firstVabSlot = 4,
              .lastVabSlot = 7,
              .loader = "sound.c: loadDigimonSounds"},
    SoundFile{.path = "SOUND/VHB/ESALL.VHB",
              .name = "esall",
              .layout = SoundFileLayout::MapSlices,
              .entrySectors = 0,
              .entryCount = 22,
              .firstVabSlot = 8,
              .lastVabSlot = 8,
              .loader = "sound.c: loadMapSounds"},
};

} // namespace

core::Result<Vhb> readVhb(std::span<const std::byte> bytes) {
  const ByteReader reader{bytes};
  const auto vhOffset = reader.u32(0);
  const auto vbOffset = reader.u32(4);
  const auto third = reader.u32(8);
  if (!vhOffset || !vbOffset || !third) {
    return core::fail(std::format("VHB of {} bytes is shorter than its header", bytes.size()));
  }
  const bool musicFont = *vhOffset == FontHeaderSize;
  if (*vhOffset != BankHeaderSize && !musicFont) {
    return core::fail(
        std::format("VHB header says the VH starts at {}; it is 12 or 16", *vhOffset));
  }
  if (*vbOffset < *vhOffset || *vbOffset > bytes.size()) {
    return core::fail(
        std::format("VHB VB offset {} is outside a file of {} bytes", *vbOffset, bytes.size()));
  }
  auto vab = readVab(bytes.subspan(*vhOffset, *vbOffset - *vhOffset));
  if (!vab) {
    return std::unexpected{vab.error()};
  }
  if (*vbOffset - *vhOffset != vab->size()) {
    return core::fail(std::format("VHB holds {} bytes between VH and VB, but the VH is {} bytes",
                                  *vbOffset - *vhOffset, vab->size()));
  }
  const std::size_t vbSize = vab->vbSize();
  if (vbSize > bytes.size() - *vbOffset) {
    return core::fail(std::format("VHB VB of {} bytes at {} runs past a file of {} bytes", vbSize,
                                  *vbOffset, bytes.size()));
  }
  const std::size_t vbEnd = *vbOffset + vbSize;
  Vhb vhb{.vhOffset = *vhOffset,
          .vbOffset = *vbOffset,
          .end = *third,
          .musicFont = musicFont,
          .vab = std::move(*vab),
          .vb = bytes.subspan(*vbOffset, vbSize),
          .seqBank = {}};
  if (*third != vbEnd) {
    return core::fail(std::format("VHB says the {} ends at {}; the VB ends at {}",
                                  musicFont ? "VB" : "bank", *third, vbEnd));
  }
  if (musicFont) {
    const auto seqEnd = reader.u32(12);
    if (!seqEnd || *seqEnd < *third || *seqEnd > bytes.size()) {
      return core::fail(
          std::format("VHB music font's SEQs end outside a file of {} bytes", bytes.size()));
    }
    vhb.end = *seqEnd;
    vhb.seqBank = bytes.subspan(*third, *seqEnd - *third);
  }
  return vhb;
}

std::span<const SoundFile> soundFiles() {
  return SoundFiles;
}

const SoundFile* findSoundFile(std::string_view path) {
  for (const auto& file : SoundFiles) {
    if (file.path == path) {
      return &file;
    }
  }
  return nullptr;
}

std::vector<SectorRange> fixedStrideEntries(const SoundFile& file) {
  std::vector<SectorRange> entries;
  entries.reserve(file.entryCount);
  for (std::uint32_t i = 0; i < file.entryCount; ++i) {
    entries.push_back(SectorRange{.first = i * file.entrySectors, .count = file.entrySectors});
  }
  return entries;
}

core::Result<std::vector<std::span<const std::byte>>>
splitSoundFile(std::span<const std::byte> bytes, std::span<const SectorRange> entries) {
  std::vector<std::span<const std::byte>> parts;
  for (const auto& entry : entries) {
    const std::size_t offset = std::size_t{entry.first} * SoundSectorSize;
    const std::size_t size = std::size_t{entry.count} * SoundSectorSize;
    if (!ByteReader{bytes}.contains(offset, size)) {
      return core::fail(std::format("sound entry {} (sectors {}+{}) is outside a file of {} bytes",
                                    parts.size(), entry.first, entry.count, bytes.size()));
    }
    parts.push_back(bytes.subspan(offset, size));
  }
  return parts;
}

} // namespace dwe::formats::audio
