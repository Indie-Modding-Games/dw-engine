#pragma once

#include <lua.hpp>

#include <string>
#include <string_view>

namespace dwe::script::detail {

// Text that came from a script (a printed line, an error message), cut to a size no message needs:
// the log keeps it outside the script's memory limit.
std::string clippedScriptText(std::string_view text);

// print for the engine's own scripts, written to the log as `lua: ...`. Like a pack's print, it
// logs a limited number of lines per call, so a looping script cannot flood the log.
int printToLog(lua_State* state);

class PackLogs;

// Pushes the print of a pack's environment, which writes `mod <pack>: ...` through logs, which must
// outlive it.
void pushModPrint(lua_State* state, std::string_view pack, PackLogs& logs);

} // namespace dwe::script::detail
