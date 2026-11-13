#pragma once

#include <dwe/core/assert.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dwe::test_support {

// A file of a .zip made by zipArchive, and what its headers say: by default a plain file, its
// bytes in deflate stored blocks, as a Unix zip tool writes it.
struct ZipFileSpec {
  std::string name;
  std::string data;
  // 0 stored, 8 deflate; any other number is written as is.
  std::uint16_t method = 8;
  // The bytes in the archive, when not made from data here (a real deflate stream, a forged one).
  std::optional<std::string> stored;
  // The expanded size the headers declare, when not data's.
  std::optional<std::uint32_t> size;
  std::uint16_t flags = 0;
  // The system that made it (high byte) and the spec version: 3 is Unix, 0 MS-DOS.
  std::uint16_t madeBy = 0x031e;
  // The external attributes: a Unix regular file, rw-r--r--, by default.
  std::uint32_t attributes = 0100644U << 16U;
};

// A plain file of a .zip, its bytes in deflate stored blocks.
inline ZipFileSpec zipFile(std::string name, std::string data = {}) {
  ZipFileSpec file;
  file.name = std::move(name);
  file.data = std::move(data);
  return file;
}

// A folder entry of a .zip, as a Unix zip tool writes it.
inline ZipFileSpec zipFolder(std::string name) {
  ZipFileSpec folder = zipFile(std::move(name));
  folder.method = 0;
  folder.attributes = 0040755U << 16U;
  return folder;
}

// The CRC-32 a .zip keeps of each file.
inline std::uint32_t zipCrc32(std::string_view bytes) {
  static const std::array<std::uint32_t, 256> table = [] {
    std::array<std::uint32_t, 256> values{};
    for (std::uint32_t n = 0; n < values.size(); ++n) {
      std::uint32_t c = n;
      for (int bit = 0; bit < 8; ++bit) {
        c = (c & 1U) != 0 ? 0xedb88320U ^ (c >> 1U) : c >> 1U;
      }
      values[n] = c;
    }
    return values;
  }();
  std::uint32_t crc = 0xffffffffU;
  for (const char c : bytes) {
    crc = table[(crc ^ static_cast<unsigned char>(c)) & 0xffU] ^ (crc >> 8U);
  }
  return crc ^ 0xffffffffU;
}

// Bytes as a deflate stream of stored blocks: valid deflate any reader expands, with no compressor.
inline std::string deflateStored(std::string_view data) {
  constexpr std::size_t Block = 0xffff;
  std::string out;
  do {
    const std::size_t size = std::min(Block, data.size());
    const bool last = size == data.size();
    out += static_cast<char>(last ? 1 : 0);
    out += static_cast<char>(size & 0xffU);
    out += static_cast<char>(size >> 8U);
    out += static_cast<char>(~size & 0xffU);
    out += static_cast<char>((~size >> 8U) & 0xffU);
    out.append(data.substr(0, size));
    data.remove_prefix(size);
  } while (!data.empty());
  return out;
}

namespace detail {

inline void put16(std::string& out, std::uint32_t value) {
  out += static_cast<char>(value & 0xffU);
  out += static_cast<char>((value >> 8U) & 0xffU);
}

inline void put32(std::string& out, std::uint32_t value) {
  put16(out, value & 0xffffU);
  put16(out, value >> 16U);
}

} // namespace detail

// A .zip archive of the files, in that order, as the format lays it out (APPNOTE.TXT).
inline std::string zipArchive(std::span<const ZipFileSpec> files) {
  std::string out;
  std::string directory;
  for (const ZipFileSpec& file : files) {
    const std::string stored =
        file.stored.value_or(file.method == 8 ? deflateStored(file.data) : file.data);
    const auto size = file.size.value_or(static_cast<std::uint32_t>(file.data.size()));
    const std::uint32_t crc = zipCrc32(file.data);
    const auto offset = static_cast<std::uint32_t>(out.size());
    const auto header = [&](std::string& to, bool central) {
      detail::put32(to, central ? 0x02014b50U : 0x04034b50U);
      if (central) {
        detail::put16(to, file.madeBy);
      }
      detail::put16(to, 20);
      detail::put16(to, file.flags);
      detail::put16(to, file.method);
      detail::put16(to, 0);    // time
      detail::put16(to, 0x21); // 1980-01-01
      detail::put32(to, crc);
      detail::put32(to, static_cast<std::uint32_t>(stored.size()));
      detail::put32(to, size);
      detail::put16(to, static_cast<std::uint32_t>(file.name.size()));
      detail::put16(to, 0); // extra
      if (central) {
        detail::put16(to, 0); // comment
        detail::put16(to, 0); // disk
        detail::put16(to, 0); // internal attributes
        detail::put32(to, file.attributes);
        detail::put32(to, offset);
      }
      to += file.name;
    };
    header(out, false);
    out += stored;
    header(directory, true);
  }
  const auto directoryOffset = static_cast<std::uint32_t>(out.size());
  out += directory;
  detail::put32(out, 0x06054b50U);
  detail::put16(out, 0);
  detail::put16(out, 0);
  detail::put16(out, static_cast<std::uint32_t>(files.size()));
  detail::put16(out, static_cast<std::uint32_t>(files.size()));
  detail::put32(out, static_cast<std::uint32_t>(directory.size()));
  detail::put32(out, directoryOffset);
  detail::put16(out, 0); // comment
  return out;
}

// Writes zipArchive(files) to a file, making its folder.
inline void writeZip(const std::filesystem::path& path, std::span<const ZipFileSpec> files) {
  std::filesystem::create_directories(path.parent_path());
  const std::string bytes = zipArchive(files);
  std::ofstream stream{path, std::ios::binary};
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  DWE_ASSERT(stream.good());
}

// The files of a folder, at any depth, as a .zip's files under `prefix` (such as "clock/", or ""
// for the root), sorted by path.
inline std::vector<ZipFileSpec> zipFilesOf(const std::filesystem::path& folder,
                                           std::string_view prefix = {}) {
  std::vector<ZipFileSpec> files;
  for (const auto& entry : std::filesystem::recursive_directory_iterator{folder}) {
    if (!entry.is_regular_file()) {
      continue;
    }
    std::ifstream stream{entry.path(), std::ios::binary};
    files.push_back(zipFile(
        std::string{prefix} + entry.path().lexically_relative(folder).generic_string(),
        std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}}));
  }
  std::ranges::sort(files, {}, &ZipFileSpec::name);
  return files;
}

} // namespace dwe::test_support
