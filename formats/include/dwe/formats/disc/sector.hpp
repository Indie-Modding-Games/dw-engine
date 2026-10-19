#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace dwe::formats::disc {

// Size of a raw CD sector: sync, header, subheader, user data and error correction.
constexpr std::size_t RawSectorSize = 2352;
// User data of a mode 1 or mode 2 form 1 sector, and the sector size of a plain ISO image.
constexpr std::size_t Form1DataSize = 2048;
// User data of a mode 2 form 2 sector (no error correction; video and XA audio).
constexpr std::size_t Form2DataSize = 2324;

// How the sectors of an image file are stored.
enum class SectorLayout : std::uint8_t {
    Raw,    // 2352 bytes per sector, as the drive reads them (BIN)
    Cooked, // 2048 bytes of user data per sector (ISO)
};

// Returns the bytes one sector takes in an image with this layout.
constexpr std::size_t sectorSize(SectorLayout layout) {
    return layout == SectorLayout::Raw ? RawSectorSize : Form1DataSize;
}

// Which kind of user data a sector holds.
enum class SectorForm : std::uint8_t {
    Form1, // 2048 bytes: mode 1, mode 2 form 1, or any sector of a cooked image
    Form2, // 2324 bytes: mode 2 form 2
};

// The subheader of a mode 2 sector (CD-ROM XA): the file and channel of an interleaved stream the sector belongs to,
// what it holds (submode bits) and, for audio, how the samples are coded.
struct XaSubheader {
    std::uint8_t file = 0;
    std::uint8_t channel = 0;
    std::uint8_t submode = 0;
    std::uint8_t coding = 0;

    bool operator==(const XaSubheader&) const = default;
};

// The user data of one sector. The span points into the buffer the sector was decoded from.
struct SectorData {
    SectorForm form;
    std::span<const std::byte> userData;
    // Only mode 2 sectors of a raw image have one; a cooked image does not keep it.
    std::optional<XaSubheader> subheader;
};

// Decodes one stored sector: checks the sync pattern and the mode of a raw sector and returns its user data.
core::Result<SectorData> decodeSector(std::span<const std::byte> sector, SectorLayout layout);

// Gives the sectors of a disc image by logical block address (LBA).
class SectorSource {
public:
    SectorSource() = default;
    SectorSource(const SectorSource&) = delete;
    SectorSource& operator=(const SectorSource&) = delete;
    virtual ~SectorSource() = default;

    // Returns the number of sectors in the image.
    [[nodiscard]] virtual std::uint32_t sectorCount() const = 0;

    // Reads and decodes one sector. The user data stays valid until the next read.
    virtual core::Result<SectorData> readSector(std::uint32_t lba) = 0;

protected:
    SectorSource(SectorSource&&) = default;
    SectorSource& operator=(SectorSource&&) = default;
};

// A disc image that is already in memory, such as a test fixture.
class MemorySectorSource final : public SectorSource {
public:
    // A partial sector at the end of the image is ignored.
    MemorySectorSource(std::span<const std::byte> image, SectorLayout layout);

    [[nodiscard]] std::uint32_t sectorCount() const override;
    core::Result<SectorData> readSector(std::uint32_t lba) override;

private:
    std::span<const std::byte> image_;
    SectorLayout layout_;
};

} // namespace dwe::formats::disc
