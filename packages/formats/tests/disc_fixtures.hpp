#pragma once

// Synthetic disc fixtures built in memory: raw sectors, a small ISO9660 volume and a fake
// executable. Nothing here comes from a real disc.

#include <dwe/formats/disc/sector.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fixtures {

using dwe::formats::disc::Form1DataSize;
using dwe::formats::disc::Form2DataSize;
using dwe::formats::disc::RawSectorSize;
using dwe::formats::disc::SectorForm;

inline void putU8(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xFFU);
}

inline void putU16(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) {
  putU8(bytes, offset, value);
  putU8(bytes, offset + 1, value >> 8U);
}

inline void putU32(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) {
  putU16(bytes, offset, value);
  putU16(bytes, offset + 2, value >> 16U);
}

// ISO9660 "both-endian" field: little-endian then big-endian copy.
inline void putU32Both(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) {
  putU32(bytes, offset, value);
  for (std::size_t i = 0; i < 4; ++i) {
    putU8(bytes, offset + 4 + i, value >> (8U * (3 - i)));
  }
}

inline void putText(std::span<std::byte> bytes, std::size_t offset, std::string_view text) {
  for (std::size_t i = 0; i < text.size(); ++i) {
    bytes[offset + i] = static_cast<std::byte>(text[i]);
  }
}

// Wraps user data in a raw sector: sync, header with the given mode, subheader (mode 2) and the
// data.
inline std::vector<std::byte> makeRawSector(std::span<const std::byte> userData, int mode,
                                            SectorForm form) {
  std::vector<std::byte> sector(RawSectorSize);
  putU8(sector, 0, 0x00);
  for (std::size_t i = 1; i < 11; ++i) {
    putU8(sector, i, 0xFF);
  }
  putU8(sector, 11, 0x00);
  putU8(sector, 15, static_cast<std::uint32_t>(mode));
  std::size_t dataOffset = 16;
  if (mode == 2) {
    const std::uint32_t submode = form == SectorForm::Form2 ? 0x20U : 0x08U;
    putU8(sector, 18, submode);
    putU8(sector, 22, submode);
    dataOffset = 24;
  }
  const std::size_t capacity = form == SectorForm::Form2 ? Form2DataSize : Form1DataSize;
  for (std::size_t i = 0; i < userData.size() && i < capacity; ++i) {
    sector[dataOffset + i] = userData[i];
  }
  return sector;
}

// Turns a cooked 2048-byte image into a raw mode 2 form 1 image.
inline std::vector<std::byte> toRawImage(std::span<const std::byte> cooked) {
  std::vector<std::byte> raw;
  for (std::size_t offset = 0; offset + Form1DataSize <= cooked.size(); offset += Form1DataSize) {
    const auto sector = makeRawSector(cooked.subspan(offset, Form1DataSize), 2, SectorForm::Form1);
    raw.insert(raw.end(), sector.begin(), sector.end());
  }
  return raw;
}

// Writes an ISO9660 directory record at offset and returns the offset after it.
inline std::size_t putDirectoryRecord(std::span<std::byte> sector, std::size_t offset,
                                      std::string_view name, std::uint32_t lba, std::uint32_t size,
                                      bool directory) {
  const std::size_t length = 33 + name.size() + (name.size() % 2 == 0 ? 1 : 0);
  putU8(sector, offset, static_cast<std::uint32_t>(length));
  putU32Both(sector, offset + 2, lba);
  putU32Both(sector, offset + 10, size);
  putU8(sector, offset + 25, directory ? 0x02U : 0x00U);
  putU16(sector, offset + 28, 1);
  putU8(sector, offset + 32, static_cast<std::uint32_t>(name.size()));
  putText(sector, offset + 33, name);
  return offset + length;
}

// Builds a small cooked ISO9660 image: the volume descriptors at sectors 16 and 17, one sector per
// directory from sector 18 (the root first, then the others by path) and then the files, by path.
class IsoBuilder {
public:
  static constexpr std::uint32_t RootLba = 18;

  // Adds a file; its folders are created as needed.
  void addFile(const std::string& path, std::vector<std::byte> contents) {
    files_[path] = std::move(contents);
  }

  std::vector<std::byte> build() {
    std::set<std::string> directories{""};
    for (const auto& [path, contents] : files_) {
      for (std::string parent = parentOf(path); !parent.empty(); parent = parentOf(parent)) {
        directories.insert(parent);
      }
    }
    std::uint32_t next = RootLba;
    for (const auto& directory : directories) {
      lbas_[directory] = next++;
    }
    for (const auto& [path, contents] : files_) {
      lbas_[path] = next;
      next += std::max<std::uint32_t>(1, sectorsFor(contents.size()));
    }

    std::vector<std::byte> image(std::size_t{next} * Form1DataSize);
    const auto sector = [&image](std::uint32_t lba) {
      return std::span{image}.subspan(std::size_t{lba} * Form1DataSize, Form1DataSize);
    };
    const auto primary = sector(16);
    putU8(primary, 0, 1);
    putText(primary, 1, "CD001");
    putU8(primary, 6, 1);
    putText(primary, 40, "FIXTURE                         ");
    putU32Both(primary, 80, next);
    putU16(primary, 128, static_cast<std::uint32_t>(Form1DataSize));
    putDirectoryRecord(primary, 156, std::string_view{"\0", 1}, RootLba, Form1DataSize, true);
    const auto terminator = sector(17);
    putU8(terminator, 0, 0xFF);
    putText(terminator, 1, "CD001");
    putU8(terminator, 6, 1);

    for (const auto& directory : directories) {
      const auto out = sector(lbas_[directory]);
      std::size_t offset = putDirectoryRecord(out, 0, std::string_view{"\0", 1}, lbas_[directory],
                                              Form1DataSize, true);
      offset = putDirectoryRecord(out, offset, std::string_view{"\1", 1},
                                  lbas_[parentOf(directory)], Form1DataSize, true);
      for (const auto& child : directories) {
        if (!child.empty() && parentOf(child) == directory) {
          offset =
              putDirectoryRecord(out, offset, nameOf(child), lbas_[child], Form1DataSize, true);
        }
      }
      for (const auto& [path, contents] : files_) {
        if (parentOf(path) == directory) {
          offset = putDirectoryRecord(out, offset, nameOf(path) + ";1", lbas_[path],
                                      static_cast<std::uint32_t>(contents.size()), false);
        }
      }
    }
    for (const auto& [path, contents] : files_) {
      std::ranges::copy(contents,
                        image.begin() + static_cast<std::ptrdiff_t>(lbas_[path] * Form1DataSize));
    }
    return image;
  }

  // Returns the first sector of a file or folder of the last image built ("" is the root).
  [[nodiscard]] std::uint32_t lbaOf(const std::string& path) const { return lbas_.at(path); }

private:
  static std::string parentOf(const std::string& path) {
    const auto slash = path.rfind('/');
    return slash == std::string::npos ? std::string{} : path.substr(0, slash);
  }

  static std::string nameOf(const std::string& path) {
    const auto slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
  }

  static std::uint32_t sectorsFor(std::size_t size) {
    return static_cast<std::uint32_t>((size + Form1DataSize - 1) / Form1DataSize);
  }

  std::map<std::string, std::vector<std::byte>> files_;
  std::map<std::string, std::uint32_t> lbas_;
};

// Returns size bytes counting up from seed, so every file of a fixture has different contents.
inline std::vector<std::byte> patternBytes(std::size_t size, std::uint8_t seed) {
  std::vector<std::byte> bytes(size);
  for (std::size_t i = 0; i < size; ++i) {
    bytes[i] = static_cast<std::byte>((seed + i) & 0xFFU);
  }
  return bytes;
}

// An entry of a fake executable's file table.
struct TableEntry {
  std::int32_t sector;
  std::int32_t size;
  std::string name;
};

// A fake PS-X EXE: header, then a 4 KB text segment loaded at LoadAddress with the file table at
// TableAddress (the given entries and a terminating one) and the names after it.
struct FakeExecutable {
  static constexpr std::uint32_t LoadAddress = 0x80010000;
  static constexpr std::uint32_t TableAddress = LoadAddress + 0x100;
  static constexpr std::size_t HeaderSize = 0x800;
  static constexpr std::size_t TextSize = 0x1000;
  static constexpr std::size_t NamesOffset = 0x800;

  static std::vector<std::byte> build(std::span<const TableEntry> entries) {
    std::vector<std::byte> exe(HeaderSize + TextSize);
    putText(exe, 0, "PS-X EXE");
    putU32(exe, 0x10, LoadAddress);
    putU32(exe, 0x18, LoadAddress);
    putU32(exe, 0x1C, static_cast<std::uint32_t>(TextSize));
    const auto text = std::span{exe}.subspan(HeaderSize);
    std::size_t nameOffset = NamesOffset;
    const auto putEntry = [&](std::size_t index, std::int32_t sector, std::int32_t size,
                              std::string_view name) {
      const std::size_t offset = (TableAddress - LoadAddress) + (index * 12);
      putU32(text, offset, static_cast<std::uint32_t>(sector));
      putU32(text, offset + 4, static_cast<std::uint32_t>(size));
      putU32(text, offset + 8, LoadAddress + static_cast<std::uint32_t>(nameOffset));
      putText(text, nameOffset, name);
      nameOffset += name.size() + 1;
    };
    for (std::size_t i = 0; i < entries.size(); ++i) {
      putEntry(i, entries[i].sector, entries[i].size, entries[i].name);
    }
    putEntry(entries.size(), 0, 0, "NULL");
    return exe;
  }
};

} // namespace fixtures
