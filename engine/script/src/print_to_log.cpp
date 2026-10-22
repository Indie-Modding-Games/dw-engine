#include "print_to_log.hpp"

#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

#include "address_free_text.hpp"
#include "budget.hpp"
#include "messages.hpp"
#include "pack_logs.hpp"

namespace dwe::script::detail {
namespace {

// The longest text of a script the log keeps, in bytes.
constexpr std::size_t MaxScriptTextBytes = 4096;

// The lines one call may print.
constexpr std::size_t MaxLinesPerCall = 100;

constexpr int PackUpvalue = 1;
constexpr int LogsUpvalue = 2;

// Joins the arguments with tabs, as Lua's print does. The line is built in a Lua buffer because a
// __tostring metamethod may raise a Lua error, which unwinds with longjmp and would skip the
// destructor of a C++ string.
std::string_view printedLine(lua_State* state, int count) {
  luaL_Buffer line;
  luaL_buffinit(state, &line);
  for (int i = 1; i <= count; ++i) {
    if (i > 1) {
      luaL_addchar(&line, '\t');
    }
    pushAddressFreeText(state, i);
    luaL_addvalue(&line);
  }
  luaL_pushresult(&line);
  std::size_t length = 0;
  const char* text = lua_tolstring(state, -1, &length);
  return {text, length};
}

// What printing one more line in the running call does.
enum class PrintOutcome : std::uint8_t {
  Written,
  // The first line past the limit: the log says the rest are left out.
  LimitReached,
  LeftOut,
};

PrintOutcome countPrinted(lua_State* state) {
  const std::size_t printed = ++budgetOf(state).linesPrinted;
  if (printed <= MaxLinesPerCall) {
    return PrintOutcome::Written;
  }
  return printed == MaxLinesPerCall + 1 ? PrintOutcome::LimitReached : PrintOutcome::LeftOut;
}

// The print of a pack: upvalue 1 is the pack's id, upvalue 2 the PackLogs that limit what it
// writes.
int printForMod(lua_State* state) {
  const std::string_view line = printedLine(state, lua_gettop(state));
  const PrintOutcome outcome = countPrinted(state);
  if (outcome == PrintOutcome::LeftOut) {
    return 0;
  }
  std::size_t length = 0;
  const char* packText = lua_tolstring(state, lua_upvalueindex(PackUpvalue), &length);
  const std::string_view pack{packText, length};
  auto* logs = static_cast<PackLogs*>(lua_touserdata(state, lua_upvalueindex(LogsUpvalue)));
  const std::string writer = core::formatMessage(messages::ModWriter, pack);
  if (outcome == PrintOutcome::LimitReached) {
    logs->info(pack, core::formatMessage(messages::PrintedTooMuch, writer, MaxLinesPerCall));
  } else {
    logs->info(pack, core::formatMessage(messages::PrintedLine, writer, clippedScriptText(line)));
  }
  return 0;
}

} // namespace

std::string clippedScriptText(std::string_view text) {
  if (text.size() <= MaxScriptTextBytes) {
    return std::string{text};
  }
  return core::formatMessage(messages::ClippedText, text.substr(0, MaxScriptTextBytes),
                             text.size() - MaxScriptTextBytes);
}

int printToLog(lua_State* state) {
  const std::string_view line = printedLine(state, lua_gettop(state));
  switch (countPrinted(state)) {
  case PrintOutcome::Written:
    core::logInfo("{}", core::formatMessage(messages::PrintedLine, messages::EngineWriter,
                                            clippedScriptText(line)));
    break;
  case PrintOutcome::LimitReached:
    core::logInfo("{}", core::formatMessage(messages::PrintedTooMuch, messages::EngineWriter,
                                            MaxLinesPerCall));
    break;
  case PrintOutcome::LeftOut:
    break;
  }
  return 0;
}

void pushModPrint(lua_State* state, std::string_view pack, PackLogs& logs) {
  lua_pushlstring(state, pack.data(), pack.size());
  lua_pushlightuserdata(state, &logs);
  lua_pushcclosure(state, printForMod, 2);
}

} // namespace dwe::script::detail
