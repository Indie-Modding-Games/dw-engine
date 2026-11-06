#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/script/script_listing.hpp>
#include <dwe/formats/script/structuring.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

// A story module (story/<n>.lua) read back into the listing and the structure the walks read
// (guarded_walk.hpp), so the map changes of a script a mod edited in Lua come from its Lua and not
// from the listing it started from (docs/architecture/warps-de-script.md#desde-el-lua).
//
// It reads the Lua that the emitter writes (lua_emitter.hpp) and the same forms written by hand:
// each `story.<name>(...)` call is the op the story API names so, an `if` is an IF whose entries
// are its condition read left to right (only `and`, `or`, `not` and the reads the emitter writes:
// a chain such as `(a or b) and c`, not `(a and b) or (c and d)`), `while true do` a loop,
// `do local choice <const> = story.set_selection(...)` and `do local value <const> = story.var(n)`
// a choice whose arms are an `if choice == 0 ... elseif choice == 1 ... else ... end` chain, a
// state machine as the emitter writes it, `if routines.<f>() == ENDED then return ENDED end` a
// jump_and_link, `return routines.<f>()` a jump to shared code, and `return`, `return ENDED` and
// `return story.<name>(...)` the ends. Anything else is an error that names its line: the walk
// cannot follow code it does not know, and a guess would give the field warps that are not there.
namespace dwe::formats::script {

// The line map of a module, story/<n>_lines (luaLinesToYaml): each line that runs an instruction
// and its offset.
using LuaLineMap = std::map<std::size_t, std::uint32_t>;

// Reads a line map: a mapping of line numbers (from 1) to offsets ("0x0040" or a number).
core::Result<LuaLineMap> readLuaLineMap(const core::YamlNode& lines);

// A module read back.
struct LuaListing {
  std::uint16_t script = 0; // the script id the module returns
  ScriptListing listing;
  StructuredScript structured;
  // Whether the offsets are the script's, from a line map that matches the module. Without one
  // (none given, or one that a later edit of the Lua left behind) every instruction is numbered in
  // the order the module has them, which no place outside the module means.
  bool offsets = true;
  // Why the line map was not used, when one was given.
  std::optional<std::string> lineMapProblem;
};

// Reads a module. With a line map that matches it (every line that runs an instruction has an
// offset, every offset is on such a line, a function at_<hex> starts at that offset and two lines
// with one offset run the same instruction), instructions that the emitter copied into several
// places are one instruction again, as in the listing. Fails on Lua the reader does not know, with
// the line.
core::Result<LuaListing> readLuaModule(std::string_view code, const LuaLineMap* lines = nullptr);

} // namespace dwe::formats::script
