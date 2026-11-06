#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/script/script_listing.hpp>
#include <dwe/formats/script/structuring.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

// A story patch (story/patches/*.lua, docs/modding/historia.md) read for the walks: the sections
// it gives, as code the module can take in. Only the shape the reader can follow: at the top,
// locals that only name story's own (`local story <const> = story`, `local ENDED <const> =
// story.ENDED`) and, once, `local routines = {}`, after which go the patch's private routines
// (`function routines.<name>() ... end`, no parameters, the body of a module's routine); then
// `return { script = <id>, sections = { [<id>] = function(original) ... end } }`. In a section,
// `original()` is called as a routine is (`return original()`, `if original() == ENDED then return
// ENDED end`) and nothing else of `original`; a private routine has no `original`. In both,
// `routines.<name>` is the patch's own routine: the module's routines are out of a patch's reach,
// so one the patch does not define is an error.
struct LuaPatchSection {
  std::uint16_t id = 0;
  // The function's body, cut where it calls the original: between two pieces goes a call.
  std::vector<std::string> pieces;
  std::size_t line = 0; // the patch's line where the body starts
};

// A patch's private routine: `function routines.<name>() <body> end`.
struct LuaPatchRoutine {
  std::string name;
  std::string body;
  std::size_t line = 0; // the patch's line where the body starts
};

struct LuaPatch {
  std::string name; // how errors name it, e.g. "my_mod:story/patches/a.lua"
  std::uint16_t script = 0;
  std::vector<LuaPatchSection> sections;
  std::vector<LuaPatchRoutine> routines;
};

// Reads a story patch; fails naming `name` and the line on a shape the reader cannot follow.
core::Result<LuaPatch> readLuaPatch(std::string_view code, std::string name);

// A story module with patches applied: Lua that readLuaModule reads, where each patched section's
// table entry runs the patch's body as a function of the module's routines table.
struct ComposedLuaModule {
  std::string code;
  // The composed lines that come from a patch's lines.
  struct PatchLines {
    std::size_t first = 0;
    std::size_t last = 0;
    std::string patch;
    std::size_t patchLine = 0; // the patch's line of `first`
  };
  std::vector<PatchLines> patchLines;

  // A readLuaModule error on `code`: its "line N" in a patch named by the patch and its line.
  [[nodiscard]] std::string located(std::string_view message) const;
};

// Applies `patches` to the module `module` of script `script` (none: a script only patches give):
// the last patch in the span that gives a section wins it, as the game's stack order does, so pass
// them bottom of the pack stack first. A patched section becomes routines.patch_<id>; the private
// routines of a patch that wins a section become routines.patch<n>_<name> (n: the patch's place in
// the span, from 1), its references rewritten, so they clash with no other patch's; a name the
// module already has takes trailing underscores until it is free. Fails when a patch calls the
// original of a section the module does not have, or when the module's own Lua does not read.
core::Result<ComposedLuaModule> composeLuaPatches(std::optional<std::string_view> module,
                                                  std::uint16_t script,
                                                  std::span<const LuaPatch> patches);

} // namespace dwe::formats::script
