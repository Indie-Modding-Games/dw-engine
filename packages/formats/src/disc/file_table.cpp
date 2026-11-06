#include <dwe/formats/byte_reader.hpp>
#include <dwe/formats/disc/file_table.hpp>
#include <dwe/formats/exe/psx_exe.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <utility>

namespace dwe::formats::disc {

namespace {

constexpr std::size_t FileEntrySize = 12;
// A mode 2 sector without its sync and header: what the original reads per sector of a video
// stream.
constexpr std::uint64_t Mode2SectorSize = 2336;
// Longer than any name in the table; a pointer that finds no NUL within it is not a name.
constexpr std::size_t MaxFilenameLength = 64;

// The address is FILE_OFFSET_TABLE in config/<version>/symbols.txt of the decomp and the capacity
// the size of the array in src/main/file_table_<version>.c. Only the US release has been checked
// against a disc.
constexpr std::array FileTableLocations = {
    FileTableLocation{.version = "us", .address = 0x8011FE1C, .capacity = 819},
    FileTableLocation{.version = "eu", .address = 0x80125F44, .capacity = 851},
    FileTableLocation{.version = "jp", .address = 0x80125E18, .capacity = 816},
    FileTableLocation{.version = "jp_rev1", .address = 0x80126730, .capacity = 816},
};

// From the decomp: src/main/script_interp.c (readFileSection), src/main/efe.c (the EFE file),
// src/main/efe_table.c (EFEX_PATH_ARRAY) and src/mov/mov.c (MOV_MOVIES).
constexpr std::array PathSearchedFiles = {
    std::string_view{"SCN/DG.SCN"},         std::string_view{"ETCHI/EFEDAT.EFE"},
    std::string_view{"ETCDAT/FEEL_EF.TMD"}, std::string_view{"ETCDAT/EX.TIM"},
    std::string_view{"ETCDAT/EX.TMD"},      std::string_view{"MOVIE/OP1.STR"},
    std::string_view{"MOVIE/OP2.STR"},      std::string_view{"MOVIE/ED2.STR"},
    std::string_view{"MOVIE/EDR.STR"},
};

std::string_view filenameOf(std::string_view path) {
  const auto slash = path.rfind('/');
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

} // namespace

std::optional<FileTableLocation> findFileTableLocation(std::string_view version) {
  const auto found = std::ranges::find(FileTableLocations, version, &FileTableLocation::version);
  if (found == FileTableLocations.end()) {
    return std::nullopt;
  }
  return *found;
}

core::Result<std::vector<FileEntry>> readFileTable(std::span<const std::byte> executable,
                                                   const FileTableLocation& location) {
  const auto header = exe::readPsxExeHeader(executable);
  if (!header) {
    return std::unexpected{header.error()};
  }
  const auto tableOffset =
      exe::fileOffsetOf(*header, location.address, location.capacity * FileEntrySize);
  if (!tableOffset) {
    return core::fail(
        std::format("the file table at {:#010x} is outside the executable", location.address));
  }
  const ByteReader reader{executable};
  std::vector<FileEntry> entries;
  for (std::size_t i = 0; i < location.capacity; ++i) {
    const std::size_t offset = *tableOffset + (i * FileEntrySize);
    const auto sector = reader.s32(offset);
    const auto size = reader.s32(offset + 4);
    const auto namePointer = reader.u32(offset + 8);
    if (!sector || !size || !namePointer) {
      return core::fail(std::format("file table entry {} is truncated", i));
    }
    if (*size == 0) {
      return entries;
    }
    if (*sector < 0 || *size < 0) {
      return core::fail(std::format("file table entry {} has a negative sector or size", i));
    }
    const auto nameOffset = exe::fileOffsetOf(*header, *namePointer, 1);
    const auto name = nameOffset ? reader.cString(*nameOffset, MaxFilenameLength) : std::nullopt;
    if (!name || name->empty()) {
      return core::fail(
          std::format("file table entry {} has no name at {:#010x}", i, *namePointer));
    }
    entries.push_back(FileEntry{.sector = *sector, .size = *size, .filename = std::string{*name}});
  }
  return core::fail(
      std::format("the file table has no terminating entry within {} entries", location.capacity));
}

std::span<const std::string_view> pathSearchedFiles() {
  return PathSearchedFiles;
}

FileTableCheck checkFileTable(std::span<const FileEntry> table, std::span<const IsoFile> files) {
  std::multimap<std::string_view, std::size_t> byName;
  for (std::size_t i = 0; i < files.size(); ++i) {
    byName.emplace(filenameOf(files[i].path), i);
  }
  const auto sameExtent = [](const FileEntry& entry, const IsoFile& file) {
    const std::uint64_t streamSize = std::uint64_t{sectorSpan(file)} * Mode2SectorSize;
    return std::cmp_equal(file.lba, entry.sector) &&
           (std::cmp_equal(file.size, entry.size) || std::cmp_equal(streamSize, entry.size));
  };

  FileTableCheck check{.inTable = std::vector<bool>(files.size(), false),
                       .aliases = {},
                       .tableOnly = {},
                       .mismatches = {}};
  for (const auto& entry : table) {
    const auto [first, last] = byName.equal_range(entry.filename);
    const auto match = std::find_if(first, last, [&](const auto& candidate) {
      return sameExtent(entry, files[candidate.second]);
    });
    if (match != last) {
      check.inTable[match->second] = true;
      continue;
    }
    if (first != last) {
      for (auto candidate = first; candidate != last; ++candidate) {
        const IsoFile& file = files[candidate->second];
        check.mismatches.push_back(
            std::format("{}: sector {}, {} bytes in the table; {} is sector {}, {} bytes",
                        entry.filename, entry.sector, entry.size, file.path, file.lba, file.size));
      }
      continue;
    }
    const auto alias =
        std::ranges::find_if(files, [&](const IsoFile& file) { return sameExtent(entry, file); });
    if (alias != files.end()) {
      check.inTable[static_cast<std::size_t>(alias - files.begin())] = true;
      check.aliases.push_back(std::format("{} is {}", entry.filename, alias->path));
      continue;
    }
    check.tableOnly.push_back(entry);
  }
  return check;
}

} // namespace dwe::formats::disc
