#include <dwe/assets/file_system.hpp>
#include <dwe/content/zip_archive.hpp>
#include <dwe/core/message.hpp>

#include <lodepng.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <system_error>
#include <utility>

#include "messages.hpp"

namespace dwe::content {
namespace {

// The records of the format (APPNOTE.TXT): their signatures and fixed sizes.
constexpr std::uint32_t EndSignature = 0x06054b50;
constexpr std::uint32_t CentralSignature = 0x02014b50;
constexpr std::uint32_t LocalSignature = 0x04034b50;
constexpr std::size_t EndSize = 22;
constexpr std::size_t CentralSize = 46;
constexpr std::size_t LocalSize = 30;
constexpr std::size_t MaxCommentSize = 0xffff;

constexpr std::uint16_t MethodStored = 0;
constexpr std::uint16_t MethodDeflate = 8;
// AES encryption (WinZip): the method an encrypted entry declares instead of its own.
constexpr std::uint16_t MethodAes = 99;
constexpr std::uint16_t FlagEncrypted = 0x0001;
constexpr std::uint16_t FlagStrongEncryption = 0x0040;

// The systems whose external attributes carry a Unix mode in their high 16 bits.
constexpr std::uint8_t HostUnix = 3;
constexpr std::uint8_t HostMac = 19;
constexpr std::uint32_t ModeType = 0170000;
constexpr std::uint32_t ModeFile = 0100000;
constexpr std::uint32_t ModeFolder = 0040000;
// The Windows attributes in the low 16 bits: a reparse point is a link.
constexpr std::uint32_t AttributeReparsePoint = 0x0400;
constexpr std::uint32_t AttributeFolder = 0x0010;

// lodepng allocates its output with malloc unless it is built with its own allocators.
struct FreeBuffer {
  void operator()(unsigned char* buffer) const {
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory): lodepng's buffer needs free.
    std::free(buffer);
  }
};

std::uint16_t read16(std::span<const std::byte> bytes, std::size_t at) {
  return static_cast<std::uint16_t>(std::to_integer<unsigned>(bytes[at]) |
                                    (std::to_integer<unsigned>(bytes[at + 1]) << 8U));
}

std::uint32_t read32(std::span<const std::byte> bytes, std::size_t at) {
  return static_cast<std::uint32_t>(read16(bytes, at)) |
         (static_cast<std::uint32_t>(read16(bytes, at + 2)) << 16U);
}

std::string inFile(const std::filesystem::path& archive, std::string_view message) {
  return core::formatMessage(messages::ZipInFile, assets::pathText(archive), message);
}

core::Result<std::vector<std::byte>> readAt(std::ifstream& stream, std::uint64_t offset,
                                            std::size_t size) {
  std::vector<std::byte> bytes(size);
  stream.seekg(static_cast<std::streamoff>(offset));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): istream reads chars.
  stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
  if (!stream || static_cast<std::size_t>(stream.gcount()) != size) {
    return core::fail(std::string{messages::ZipNotZip});
  }
  return bytes;
}

// Whether a path of the archive stays inside it: relative, no '..' or '.', no '\' (a Windows
// separator some tools write), no ':' (a drive), no control character and no empty folder name.
// Gives its folder count, or 0 when it is not safe.
std::size_t safeDepth(std::string_view name) {
  if (name.empty() || name.front() == '/') {
    return 0;
  }
  if (std::ranges::any_of(name, [](char c) {
        const auto code = static_cast<unsigned char>(c);
        return code < 0x20 || code == 0x7f || c == '\\' || c == ':';
      })) {
    return 0;
  }
  if (name.ends_with('/')) {
    name.remove_suffix(1);
  }
  std::size_t depth = 0;
  while (true) {
    const std::size_t slash = name.find('/');
    const std::string_view part = name.substr(0, slash);
    if (part.empty() || part == "." || part == "..") {
      return 0;
    }
    ++depth;
    if (slash == std::string_view::npos) {
      return depth;
    }
    name.remove_prefix(slash + 1);
  }
}

} // namespace

core::Result<ZipArchive> ZipArchive::open(const std::filesystem::path& archive,
                                          const ZipLimits& limits) {
  const auto fail = [&](std::string_view message) { return core::fail(inFile(archive, message)); };
  std::error_code error;
  // A link could lead anywhere: the archive must be a file of the folder itself.
  if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(archive, error))) {
    return fail(messages::ZipNotFile);
  }
  const std::uint64_t fileSize = std::filesystem::file_size(archive, error);
  if (error) {
    return fail(messages::ZipCannotOpen);
  }
  if (fileSize > limits.maxArchiveBytes) {
    return fail(core::formatMessage(messages::ZipTooBig, limits.maxArchiveBytes));
  }
  if (fileSize < EndSize) {
    return fail(messages::ZipNotZip);
  }
  std::ifstream stream{archive, std::ios::binary};
  if (!stream) {
    return fail(messages::ZipCannotOpen);
  }
  // The end record is last, followed by a comment of at most 64 KiB.
  const std::uint64_t tailSize = std::min<std::uint64_t>(fileSize, EndSize + MaxCommentSize);
  const std::uint64_t tailStart = fileSize - tailSize;
  auto tail = readAt(stream, tailStart, static_cast<std::size_t>(tailSize));
  if (!tail) {
    return fail(messages::ZipNotZip);
  }
  std::size_t end = tail->size() - EndSize + 1;
  bool found = false;
  while (end-- > 0) {
    if (read32(*tail, end) == EndSignature &&
        end + EndSize + read16(*tail, end + 20) <= tail->size()) {
      found = true;
      break;
    }
  }
  if (!found) {
    return fail(messages::ZipNotZip);
  }
  const std::span<const std::byte> record = std::span{*tail}.subspan(end, EndSize);
  const std::uint16_t disk = read16(record, 4);
  const std::uint16_t directoryDisk = read16(record, 6);
  const std::uint16_t onDisk = read16(record, 8);
  const std::uint16_t count = read16(record, 10);
  const std::uint32_t directorySize = read32(record, 12);
  const std::uint32_t directoryOffset = read32(record, 16);
  if (disk != 0 || directoryDisk != 0 || onDisk != count) {
    return fail(messages::ZipSpanned);
  }
  if (count > limits.maxEntries) {
    return fail(core::formatMessage(messages::ZipTooManyEntries, limits.maxEntries));
  }
  if (static_cast<std::uint64_t>(directoryOffset) + directorySize > tailStart + end) {
    return fail(messages::ZipBrokenDirectory);
  }
  auto directory = readAt(stream, directoryOffset, directorySize);
  if (!directory) {
    return fail(messages::ZipBrokenDirectory);
  }

  ZipArchive zip;
  zip.path_ = archive;
  zip.directoryOffset_ = directoryOffset;
  std::set<std::string, std::less<>> names;
  std::uint64_t expanded = 0;
  std::size_t at = 0;
  const std::span<const std::byte> bytes{*directory};
  for (std::uint16_t index = 0; index < count; ++index) {
    if (at + CentralSize > bytes.size() || read32(bytes, at) != CentralSignature) {
      return fail(messages::ZipBrokenDirectory);
    }
    const std::uint16_t madeBy = read16(bytes, at + 4);
    const std::uint16_t flags = read16(bytes, at + 8);
    ZipEntry entry;
    entry.method = read16(bytes, at + 10);
    entry.crc32 = read32(bytes, at + 16);
    entry.storedSize = read32(bytes, at + 20);
    entry.size = read32(bytes, at + 24);
    const std::size_t nameSize = read16(bytes, at + 28);
    const std::size_t extraSize = read16(bytes, at + 30);
    const std::size_t commentSize = read16(bytes, at + 32);
    const std::uint32_t attributes = read32(bytes, at + 38);
    entry.headerOffset = read32(bytes, at + 42);
    if (at + CentralSize + nameSize + extraSize + commentSize > bytes.size()) {
      return fail(messages::ZipBrokenDirectory);
    }
    const std::span<const std::byte> nameBytes = bytes.subspan(at + CentralSize, nameSize);
    entry.name.resize(nameSize);
    std::memcpy(entry.name.data(), nameBytes.data(), nameSize);
    at += CentralSize + nameSize + extraSize + commentSize;

    const std::size_t depth = safeDepth(entry.name);
    if (depth == 0) {
      return fail(core::formatMessage(messages::ZipUnsafePath, entry.name));
    }
    if (depth > limits.maxDepth) {
      return fail(core::formatMessage(messages::ZipTooDeep, entry.name, limits.maxDepth));
    }
    if ((flags & (FlagEncrypted | FlagStrongEncryption)) != 0 || entry.method == MethodAes) {
      return fail(core::formatMessage(messages::ZipEncrypted, entry.name));
    }
    const auto host = static_cast<std::uint8_t>(madeBy >> 8U);
    const std::uint32_t mode = (host == HostUnix || host == HostMac) ? (attributes >> 16U) : 0;
    const std::uint32_t type = mode & ModeType;
    if ((type != 0 && type != ModeFile && type != ModeFolder) ||
        (attributes & AttributeReparsePoint) != 0) {
      return fail(core::formatMessage(messages::ZipSpecialEntry, entry.name));
    }
    std::string key = entry.name;
    const bool folder =
        entry.name.ends_with('/') || type == ModeFolder || (attributes & AttributeFolder) != 0;
    if (folder && !key.ends_with('/')) {
      key += '/';
    }
    if (!names.insert(std::move(key)).second) {
      return fail(core::formatMessage(messages::ZipTwice, entry.name));
    }
    if (folder) {
      if (entry.size != 0) {
        return fail(messages::ZipBrokenDirectory);
      }
      continue;
    }
    if (entry.method != MethodStored && entry.method != MethodDeflate) {
      return fail(core::formatMessage(messages::ZipMethod, entry.name, entry.method));
    }
    expanded += entry.size;
    if (expanded > limits.maxExpandedBytes) {
      return fail(core::formatMessage(messages::ZipExpandsTooMuch, limits.maxExpandedBytes));
    }
    if (entry.size > limits.ratioFloor &&
        entry.size > static_cast<std::uint64_t>(entry.storedSize) * limits.maxRatio) {
      return fail(core::formatMessage(messages::ZipBomb, entry.name,
                                      entry.size / std::max<std::uint32_t>(entry.storedSize, 1)));
    }
    if ((entry.method == MethodStored && entry.storedSize != entry.size) ||
        static_cast<std::uint64_t>(entry.headerOffset) + LocalSize + entry.storedSize >
            directoryOffset) {
      return fail(
          core::formatMessage(messages::ZipBrokenEntry, entry.name, messages::ZipBadHeader));
    }
    zip.entries_.push_back(std::move(entry));
  }
  return zip;
}

const ZipEntry* ZipArchive::find(std::string_view name) const {
  const auto found = std::ranges::find(entries_, name, &ZipEntry::name);
  return found == entries_.end() ? nullptr : &*found;
}

core::Result<std::vector<std::byte>> ZipArchive::read(const ZipEntry& entry) const {
  const auto broken = [&](std::string_view why) {
    return core::fail(
        inFile(path_, core::formatMessage(messages::ZipBrokenEntry, entry.name, why)));
  };
  std::ifstream stream{path_, std::ios::binary};
  if (!stream) {
    return core::fail(inFile(path_, messages::ZipCannotOpen));
  }
  auto header = readAt(stream, entry.headerOffset, LocalSize);
  if (!header || read32(*header, 0) != LocalSignature) {
    return broken(messages::ZipBadHeader);
  }
  const std::uint64_t start = static_cast<std::uint64_t>(entry.headerOffset) + LocalSize +
                              read16(*header, 26) + read16(*header, 28);
  if (start + entry.storedSize > directoryOffset_) {
    return broken(messages::ZipBadHeader);
  }
  auto stored = readAt(stream, start, entry.storedSize);
  if (!stored) {
    return broken(messages::ZipBadHeader);
  }
  std::vector<std::byte> bytes;
  if (entry.method == MethodStored) {
    bytes = std::move(*stored);
  } else if (entry.size > 0) {
    LodePNGDecompressSettings settings;
    lodepng_decompress_settings_init(&settings);
    // It stops once past the size the archive declares, so a lying entry cannot take more memory
    // than its declared size, which open() bounded.
    settings.max_output_size = std::max<std::size_t>(entry.size, 1);
    unsigned char* out = nullptr;
    std::size_t outSize = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): lodepng reads unsigned chars.
    const auto* in = reinterpret_cast<const unsigned char*>(stored->data());
    const unsigned failed = lodepng_inflate(&out, &outSize, in, stored->size(), &settings);
    const std::unique_ptr<unsigned char, FreeBuffer> owner{out};
    if (failed != 0) {
      return broken(outSize > entry.size ? messages::ZipBadSize : messages::ZipBadData);
    }
    if (outSize != entry.size) {
      return broken(messages::ZipBadSize);
    }
    bytes.resize(outSize);
    if (outSize > 0) {
      std::memcpy(bytes.data(), out, outSize);
    }
  }
  if (bytes.size() != entry.size) {
    return broken(messages::ZipBadSize);
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): lodepng reads unsigned chars.
  if (lodepng_crc32(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size()) !=
      entry.crc32) {
    return broken(messages::ZipBadCrc);
  }
  return bytes;
}

} // namespace dwe::content
