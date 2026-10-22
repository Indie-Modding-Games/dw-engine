#pragma once

#include <dwe/core/error.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// The event script files: SCN/MAPHEAD.SCN, which is script 0, and SCN/DG.SCN, which holds the
// others (script_interp.c: initializeScripts, getScript, getScriptSection).
namespace dwe::formats::script {

// The bytes of DG.SCN that initializeScripts reads as its offset table.
inline constexpr std::size_t ScriptTableSize = 0x2000;

// Where a script of DG.SCN is: getScript(id) reads [table[id], table[id + 1]).
struct ScriptSlot {
  std::uint32_t offset = 0;
  std::uint32_t size = 0;

  bool operator==(const ScriptSlot&) const = default;
};

// Reads the offset table of DG.SCN: u32 byte offsets indexed by script id. The table ends at the
// entry that equals the file size; the rest of the 0x2000 bytes holds leftovers and, from 0x800 on,
// the first scripts themselves, which the game reads twice. Fails if an offset goes backwards or
// past the file, or if no entry ends the table.
core::Result<std::vector<ScriptSlot>> readScriptTable(std::span<const std::byte> file);

// An entry point of a script: the engine runs a section by its id (a map id in MAPHEAD, 0xFE on
// entering a map, an NPC's script id when talking to it...).
struct ScriptSection {
  std::uint16_t id = 0;
  std::uint16_t offset = 0; // from the start of the script

  bool operator==(const ScriptSection&) const = default;
};

// The header of a script: u16 codeStart, then {u16 id, u16 offset} per section up to a 0xFFFF id.
struct ScriptHeader {
  std::uint16_t codeStart = 0; // the offset of the first instruction, right after the 0xFFFF
  std::vector<ScriptSection> sections;

  bool operator==(const ScriptHeader&) const = default;
};

// Reads the header of a script. Fails if it runs past the bytes, if codeStart is not the end of the
// section list or if a section starts outside the script.
core::Result<ScriptHeader> readScriptHeader(std::span<const std::byte> script);

} // namespace dwe::formats::script
