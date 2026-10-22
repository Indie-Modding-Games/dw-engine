#include <dwe/formats/byte_reader.hpp>
#include <dwe/formats/exe/memory_image.hpp>
#include <dwe/formats/exe/psx_exe.hpp>

#include <algorithm>
#include <format>

namespace dwe::formats::exe {

namespace {

constexpr std::uint64_t AddressSpaceSize = 0x1'0000'0000U;

} // namespace

core::Result<MemoryImage> MemoryImage::fromExecutable(std::span<const std::byte> file) {
  auto header = readPsxExeHeader(file);
  if (!header) {
    return std::unexpected{header.error()};
  }
  return MemoryImage{file.subspan(PsxExeHeaderSize, header->tSize), header->tAddr};
}

MemoryImage MemoryImage::fromOverlay(std::span<const std::byte> file, std::uint32_t loadAddress) {
  const std::uint64_t room = AddressSpaceSize - loadAddress;
  return MemoryImage{
      file.first(static_cast<std::size_t>(std::min<std::uint64_t>(file.size(), room))),
      loadAddress};
}

core::Result<std::span<const std::byte>> MemoryImage::bytesAt(std::uint32_t address,
                                                              std::size_t size) const {
  const auto bytes = address < baseAddress_
                         ? std::nullopt
                         : ByteReader{bytes_}.bytes(address - baseAddress_, size);
  if (!bytes) {
    return core::fail(
        std::format("0x{:X} bytes at 0x{:08X} are outside the image (0x{:08X}, 0x{:X} bytes)", size,
                    address, baseAddress_, bytes_.size()));
  }
  return *bytes;
}

core::Result<std::string> MemoryImage::stringAt(std::uint32_t address,
                                                std::size_t maxLength) const {
  const auto text = address < baseAddress_
                        ? std::nullopt
                        : ByteReader{bytes_}.cString(address - baseAddress_, maxLength);
  if (!text) {
    return core::fail(std::format("no string of at most {} bytes at 0x{:08X} inside the image",
                                  maxLength, address));
  }
  return std::string{*text};
}

} // namespace dwe::formats::exe
