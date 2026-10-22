#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/disc/sector.hpp>

#include <array>
#include <filesystem>
#include <fstream>

namespace dwe::formats::disc {

// A disc image file read one sector at a time, so a whole image never has to fit in memory.
class DiscImage final : public SectorSource {
public:
  // Opens a CUE sheet (and the BIN it names, relative to the sheet), a raw 2352-byte BIN or a
  // 2048-byte ISO. A file that is not a CUE sheet is raw if its first sector starts with the sync
  // pattern.
  static core::Result<DiscImage> open(const std::filesystem::path& path);

  // Returns the file that holds the sectors (the BIN of a CUE sheet).
  [[nodiscard]] const std::filesystem::path& dataFile() const { return dataFile_; }
  [[nodiscard]] SectorLayout layout() const { return layout_; }

  [[nodiscard]] std::uint32_t sectorCount() const override { return sectorCount_; }
  core::Result<SectorData> readSector(std::uint32_t lba) override;

private:
  DiscImage(std::filesystem::path dataFile, std::ifstream file, SectorLayout layout,
            std::uint32_t sectorCount);

  std::filesystem::path dataFile_;
  std::ifstream file_;
  SectorLayout layout_;
  std::uint32_t sectorCount_;
  std::array<std::byte, RawSectorSize> buffer_{};
};

} // namespace dwe::formats::disc
