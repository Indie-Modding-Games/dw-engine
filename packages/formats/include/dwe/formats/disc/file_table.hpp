#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/disc/iso9660.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::formats::disc {

// An entry of the file table in the executable (FileEntry, include/dw/file_table.h in the decomp).
struct FileEntry {
  std::int32_t sector;  // first sector (LBA)
  std::int32_t size;    // in bytes
  std::string filename; // without folder: the original looks files up by name only
};

// Where a version's executable keeps its file table (FILE_OFFSET_TABLE in
// config/<version>/symbols.txt).
struct FileTableLocation {
  std::string_view version; // as in the known release table: us, eu, jp, jp_rev1
  std::uint32_t address;
  std::size_t capacity; // entries in the array, the terminating one included
};

// Returns where the executable of a version keeps its file table, if it is known.
std::optional<FileTableLocation> findFileTableLocation(std::string_view version);

// Reads the table up to its terminating entry (size 0), following each name pointer into the
// executable.
core::Result<std::vector<FileEntry>> readFileTable(std::span<const std::byte> executable,
                                                   const FileTableLocation& location);

// Returns the volume paths the original opens by path with CdSearchFile instead of through the
// table.
std::span<const std::string_view> pathSearchedFiles();

// How the table and the volume agree.
struct FileTableCheck {
  std::vector<bool> inTable;        // per volume file: a table entry has its sector and size
  std::vector<std::string> aliases; // one line per entry that names a volume file by another name
  std::vector<FileEntry> tableOnly; // entries no volume file matches: data the directories hide
  std::vector<std::string>
      mismatches; // one line per entry whose name is on the volume at another sector or size
};

// Matches every table entry against the volume file with the same name or, failing that, the same
// sector and size (an alias). A size also matches when it counts 2336 bytes per sector: the
// original reads video streams as whole mode 2 sectors (subheader, data and error detection).
FileTableCheck checkFileTable(std::span<const FileEntry> table, std::span<const IsoFile> files);

} // namespace dwe::formats::disc
