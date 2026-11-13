#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::content {

// What a .zip a player drops in may hold. The sizes are the launcher's for content add-ons
// (apps/launcher/src-tauri/src/addons/package.rs), so the same archive is accepted by both.
struct ZipLimits {
  // The archive file itself.
  std::uint64_t maxArchiveBytes = 20ULL * 1024 * 1024;
  // Every entry together once expanded, as the archive declares it and as it really expands.
  std::uint64_t maxExpandedBytes = 40ULL * 1024 * 1024;
  // Entries, folders included.
  std::size_t maxEntries = 1000;
  // Folders in an entry's path, its name included.
  std::size_t maxDepth = 12;
  // An entry may expand to at most this many times its stored size (a zip bomb expands to far
  // more), once it expands past ratioFloor: small files of one repeated byte may go further.
  std::uint64_t maxRatio = 100;
  std::uint64_t ratioFloor = 64ULL * 1024;
};

// A file in a .zip archive, as its central directory describes it.
struct ZipEntry {
  // Its path in the archive, with '/' between folders; never a folder.
  std::string name;
  std::uint16_t method = 0; // 0 stored, 8 deflate
  std::uint32_t crc32 = 0;
  std::uint32_t storedSize = 0;
  std::uint32_t size = 0;
  // Where its local header starts.
  std::uint32_t headerOffset = 0;
};

// A .zip archive on disk, read in place: open() reads and checks its central directory, and each
// file is read and expanded only when asked for, its size and CRC-32 checked. Only stored and
// deflate entries are read (as every zip tool writes them); the archive is refused whole, before
// any file is read, when it is too big, has an absolute path, a '..', a '\' or a ':' in a path, a
// link or other special entry, an encrypted entry, two entries of one name, too many entries, too
// many bytes once expanded or an entry that expands suspiciously far (ZipLimits). Folder entries
// are checked and then dropped. Its const functions may be called from several threads at once.
class ZipArchive {
public:
  static core::Result<ZipArchive> open(const std::filesystem::path& archive,
                                       const ZipLimits& limits = {});

  [[nodiscard]] const std::filesystem::path& path() const { return path_; }
  // The files, in the archive's order.
  [[nodiscard]] std::span<const ZipEntry> entries() const { return entries_; }
  // The file of a name, or nullptr.
  [[nodiscard]] const ZipEntry* find(std::string_view name) const;

  // Reads and expands a file of the archive, checking its size and CRC-32.
  [[nodiscard]] core::Result<std::vector<std::byte>> read(const ZipEntry& entry) const;

private:
  ZipArchive() = default;

  std::filesystem::path path_;
  std::vector<ZipEntry> entries_;
  // Where the central directory starts: no file's bytes go past it.
  std::uint64_t directoryOffset_ = 0;
};

} // namespace dwe::content
