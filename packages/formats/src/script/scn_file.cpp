#include <dwe/formats/script/scn_file.hpp>

#include <bytes/byte_cursor.hpp>

#include <format>

namespace dwe::formats::script {

namespace {

constexpr std::uint16_t SectionListEnd = 0xFFFF;

} // namespace

core::Result<std::vector<ScriptSlot>> readScriptTable(std::span<const std::byte> file) {
  if (file.size() < ScriptTableSize) {
    return core::fail(std::format("the file has {} bytes, less than its {}-byte table", file.size(),
                                  ScriptTableSize));
  }
  bytes::ByteCursor table{file.first(ScriptTableSize)};
  std::vector<ScriptSlot> slots;
  std::uint32_t offset = table.u32();
  if (offset > file.size()) {
    return core::fail(std::format("script 0 starts at {:#x}, past the end of the file", offset));
  }
  while (offset != file.size()) {
    const std::uint32_t next = table.u32();
    if (table.overrun()) {
      return core::fail("no entry of the table equals the file size");
    }
    if (next < offset || next > file.size()) {
      return core::fail(std::format("script {}: [{:#x}, {:#x}) is not a range of the file",
                                    slots.size(), offset, next));
    }
    slots.push_back(ScriptSlot{.offset = offset, .size = next - offset});
    offset = next;
  }
  return slots;
}

core::Result<ScriptHeader> readScriptHeader(std::span<const std::byte> script) {
  bytes::ByteCursor c{script};
  ScriptHeader header;
  header.codeStart = c.u16();
  for (std::uint16_t id = c.u16(); id != SectionListEnd && !c.overrun(); id = c.u16()) {
    header.sections.push_back(ScriptSection{.id = id, .offset = c.u16()});
  }
  if (c.overrun()) {
    return core::fail("the section list runs past the end of the script");
  }
  if (header.codeStart != c.offset()) {
    return core::fail(std::format("the code starts at {:#x}, not after the section list at {:#x}",
                                  header.codeStart, c.offset()));
  }
  for (const ScriptSection& section : header.sections) {
    if (section.offset < header.codeStart || section.offset >= script.size()) {
      return core::fail(std::format("section {:#x} starts at {:#x}, outside the code", section.id,
                                    section.offset));
    }
  }
  return header;
}

} // namespace dwe::formats::script
