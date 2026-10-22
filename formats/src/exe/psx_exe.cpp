#include <dwe/formats/byte_reader.hpp>
#include <dwe/formats/exe/psx_exe.hpp>

#include <format>
#include <string_view>

namespace dwe::formats::exe {

namespace {

constexpr std::string_view Magic = "PS-X EXE";
constexpr std::size_t Pc0Offset = 0x10;
constexpr std::size_t Gp0Offset = 0x14;
constexpr std::size_t TAddrOffset = 0x18;
constexpr std::size_t TSizeOffset = 0x1C;

} // namespace

core::Result<PsxExeHeader> readPsxExeHeader(std::span<const std::byte> executable) {
  const ByteReader reader{executable};
  const auto magic = reader.chars(0, Magic.size());
  const auto pc0 = reader.u32(Pc0Offset);
  const auto gp0 = reader.u32(Gp0Offset);
  const auto tAddr = reader.u32(TAddrOffset);
  const auto tSize = reader.u32(TSizeOffset);
  if (!magic || !pc0 || !gp0 || !tAddr || !tSize || executable.size() < PsxExeHeaderSize) {
    return core::fail("the executable is shorter than its header");
  }
  if (*magic != Magic) {
    return core::fail("not a PS-X EXE: the magic is missing");
  }
  if (!reader.contains(PsxExeHeaderSize, *tSize)) {
    return core::fail(std::format("the text segment ({} bytes) does not fit in the file ({} bytes)",
                                  *tSize, executable.size()));
  }
  return PsxExeHeader{.pc0 = *pc0, .gp0 = *gp0, .tAddr = *tAddr, .tSize = *tSize};
}

std::optional<std::size_t> fileOffsetOf(const PsxExeHeader& header, std::uint32_t address,
                                        std::size_t count) {
  if (address < header.tAddr) {
    return std::nullopt;
  }
  const std::size_t offset = address - header.tAddr;
  if (offset > header.tSize || count > header.tSize - offset) {
    return std::nullopt;
  }
  return PsxExeHeaderSize + offset;
}

} // namespace dwe::formats::exe
