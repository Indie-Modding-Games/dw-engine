#include <dwe/formats/disc/sector.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <limits>

namespace dwe::formats::disc {

namespace {

// Raw sector layout (ECMA-130 and the CD-ROM XA extension): 12 sync bytes, a 4-byte header whose last byte is the
// mode, then for mode 2 an 8-byte subheader (two copies of file, channel, submode, coding) before the user data.
constexpr std::array<std::byte, 12> SyncPattern = {
    std::byte{0x00}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
    std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0x00},
};
constexpr std::size_t ModeOffset = 15;
constexpr std::size_t Mode1DataOffset = 16;
constexpr std::size_t SubheaderOffset = 16;
constexpr std::size_t SubmodeOffset = 18;
constexpr std::size_t Mode2DataOffset = 24;
constexpr auto SubmodeForm2 = std::byte{0x20};

} // namespace

core::Result<SectorData> decodeSector(std::span<const std::byte> sector, SectorLayout layout) {
    if (sector.size() != sectorSize(layout)) {
        return core::fail(std::format("a sector is {} bytes, expected {}", sector.size(), sectorSize(layout)));
    }
    if (layout == SectorLayout::Cooked) {
        return SectorData{.form = SectorForm::Form1, .userData = sector, .subheader = std::nullopt};
    }
    if (!std::ranges::equal(sector.first(SyncPattern.size()), SyncPattern)) {
        return core::fail("the sector has no sync pattern; the image is not a raw 2352-byte image");
    }
    switch (std::to_integer<int>(sector[ModeOffset])) {
    case 1:
        return SectorData{.form = SectorForm::Form1,
                          .userData = sector.subspan(Mode1DataOffset, Form1DataSize),
                          .subheader = std::nullopt};
    case 2: {
        const XaSubheader subheader{.file = std::to_integer<std::uint8_t>(sector[SubheaderOffset]),
                                    .channel = std::to_integer<std::uint8_t>(sector[SubheaderOffset + 1]),
                                    .submode = std::to_integer<std::uint8_t>(sector[SubheaderOffset + 2]),
                                    .coding = std::to_integer<std::uint8_t>(sector[SubheaderOffset + 3])};
        if ((sector[SubmodeOffset] & SubmodeForm2) != std::byte{0}) {
            return SectorData{.form = SectorForm::Form2,
                              .userData = sector.subspan(Mode2DataOffset, Form2DataSize),
                              .subheader = subheader};
        }
        return SectorData{.form = SectorForm::Form1,
                          .userData = sector.subspan(Mode2DataOffset, Form1DataSize),
                          .subheader = subheader};
    }
    default:
        return core::fail(std::format("unsupported sector mode {}", std::to_integer<int>(sector[ModeOffset])));
    }
}

MemorySectorSource::MemorySectorSource(std::span<const std::byte> image, SectorLayout layout)
    : image_(image), layout_(layout) {}

std::uint32_t MemorySectorSource::sectorCount() const {
    return static_cast<std::uint32_t>(
        std::min<std::size_t>(image_.size() / sectorSize(layout_), std::numeric_limits<std::uint32_t>::max()));
}

core::Result<SectorData> MemorySectorSource::readSector(std::uint32_t lba) {
    if (lba >= sectorCount()) {
        return core::fail(std::format("sector {} is past the end of the image ({} sectors)", lba, sectorCount()));
    }
    const std::size_t size = sectorSize(layout_);
    return decodeSector(image_.subspan(lba * size, size), layout_);
}

} // namespace dwe::formats::disc
