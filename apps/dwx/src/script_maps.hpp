#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/script/disassembly.hpp>

#include <cstdint>
#include <span>
#include <vector>

#include "extractor.hpp"

// Which field maps run which event scripts, so an extractor can tell where the original does
// something a script asks for (script_interp.c: tickScript).
namespace dwe::dwx {

struct ScriptCode {
  std::uint16_t id = 0;
  formats::script::Disassembly code;
};

// Reads and disassembles every script the original can run: SCN/MAPHEAD.SCN as script 0 and the
// others of SCN/DG.SCN (script_interp.c: initializeScripts, getScript).
core::Result<std::vector<ScriptCode>> readScripts(const Disc& disc);

// Where the scripts call one CALL_ROUTINE routine.
struct RoutineMaps {
  // The maps that run a calling script, sorted: the maps SET_SCRIPT loads with it, or with a script
  // that reaches it through JUMP_TO_FILE or JUMP_TO_FILE_AND_LINK (which run another script's code
  // on the same map).
  std::vector<std::uint16_t> maps;
  // The calling scripts that no SET_SCRIPT loads, directly or through a jump.
  std::vector<std::uint16_t> scriptsWithoutMap;
};

RoutineMaps findRoutineMaps(std::span<const ScriptCode> scripts, std::uint8_t routine);

} // namespace dwe::dwx
