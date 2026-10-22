#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// A synthetic overlay file: files carried before the header, the 0x40-byte header, code, data and
// the zero word that ends every overlay. The code and data are made-up bytes.
namespace dwe::formats::test {

struct OverlayFileSpec {
  std::vector<std::byte> leading; // what comes before the header
  std::uint8_t id = 1;
  std::uint32_t address = 0x80080000; // the header's own address
  std::string_view name = "TEST_REL.BIN";
  std::size_t codeSize = 0x20;
  std::vector<std::byte>
      data; // the data and the stored memory, as the header's data size covers both
};

inline void putU32(std::vector<std::byte>& file, std::size_t offset, std::uint32_t value) {
  for (std::size_t i = 0; i < 4; ++i) {
    file.at(offset + i) = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
  }
}

inline std::vector<std::byte> makeOverlayFile(const OverlayFileSpec& spec) {
  std::vector<std::byte> file = spec.leading;
  const std::size_t header = file.size();
  file.resize(header + 0x40);
  constexpr std::string_view Magic = "MWo1";
  for (std::size_t i = 0; i < Magic.size(); ++i) {
    file[header + i] = static_cast<std::byte>(Magic[i]);
  }
  file[header + 4] = std::byte{spec.id};
  putU32(file, header + 8, spec.address);
  putU32(file, header + 0xC, static_cast<std::uint32_t>(spec.codeSize));
  putU32(file, header + 0x10, static_cast<std::uint32_t>(spec.data.size()));
  for (std::size_t i = 0; i < spec.name.size(); ++i) {
    file[header + 0x20 + i] = static_cast<std::byte>(spec.name[i]);
  }
  // Code: a recognisable non-zero pattern.
  for (std::size_t i = 0; i < spec.codeSize; ++i) {
    file.push_back(static_cast<std::byte>(0x40 + (i % 0x20)));
  }
  file.insert(file.end(), spec.data.begin(), spec.data.end());
  file.resize(file.size() + 4);
  return file;
}

} // namespace dwe::formats::test
