#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace dwe::formats::exe {

// The bytes of an executable or an overlay as the console's memory sees them, so that tables can be
// read at the addresses the decomp gives. It does not own the bytes.
class MemoryImage {
public:
  // Maps a main executable: its PS-X EXE header gives the address and size of the text, which
  // starts after the 0x800-byte header.
  static core::Result<MemoryImage> fromExecutable(std::span<const std::byte> file);

  // Maps an overlay (*_REL.BIN), which has no header and is loaded whole at loadAddress
  // (utils2.c: loadDynamicLibrary).
  static MemoryImage fromOverlay(std::span<const std::byte> file, std::uint32_t loadAddress);

  [[nodiscard]] std::uint32_t baseAddress() const { return baseAddress_; }
  [[nodiscard]] std::size_t size() const { return bytes_.size(); }

  // Returns size bytes at a memory address, or an error if any of them is outside the image.
  [[nodiscard]] core::Result<std::span<const std::byte>> bytesAt(std::uint32_t address,
                                                                 std::size_t size) const;

  // Returns the bytes of the NUL-terminated string at an address, without the NUL. Fails if the
  // string leaves the image or is longer than maxLength.
  [[nodiscard]] core::Result<std::string> stringAt(std::uint32_t address,
                                                   std::size_t maxLength = 1024) const;

private:
  MemoryImage(std::span<const std::byte> bytes, std::uint32_t baseAddress)
      : bytes_(bytes), baseAddress_(baseAddress) {}

  std::span<const std::byte> bytes_;
  std::uint32_t baseAddress_;
};

} // namespace dwe::formats::exe
