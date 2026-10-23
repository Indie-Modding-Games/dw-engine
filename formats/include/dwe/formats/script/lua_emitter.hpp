#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/script/script_listing.hpp>
#include <dwe/formats/script/structuring.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The Lua module of an event script: its structured routines written out as Lua functions that call
// the `story` API, one call per instruction, with no jump statement (docs/formats/scripts.md#lua).
//
// The module returns { script = <id>, sections = { [<section id>] = <function>, ... } }. Each
// routine is a function of the table `routines`, named by the offset of its entry
// (routines.at_04c0); a section, a subroutine and a shared tail that start at the same instruction
// are one function. A function returns story.ENDED when the script ended (end_section and
// end_script end it from any depth, script_interp.c: scriptInstruction10to27) and nothing on a
// jump_return, so a jump_and_link is `if routines.at_X() == ENDED then return ENDED end` and a jump
// to shared code is the tail call `return routines.at_X()`. jump_to_file, warp_to and teleport end
// the routine with `return story.<name>(...)`, so they give back what the API returns.
//
// What the `story` API must provide, beyond one function per op (story.<name>(operands in the
// listing's order, pads and labels left out, an entity as PLAYER, PARTNER or the NPC's script id),
// with <name> the op's name in the story API (story_api_names.hpp: storyFunctionName)):
// - story.ENDED, story.PLAYER (0xFD), story.PARTNER (0xFC);
// - story.show_textbox(key) and story.set_selection(key, count), which returns the option picked
// from 0 or count for
//   a cancel, with key a text key (LuaModuleName::textSource);
// - the pure reads the IF tests use, each in the domain the original compares it in
// (script_value.c:
//   scriptCompareValue compares unsigned 32-bit values, scriptCompareSignedValue signed ones):
//   story.trigger(n) a boolean; story.var(n) 0..255; story.stat(n) 0..65535, but -32768..32767 for
//   stat 9; story.cards(n), story.items(n) the counts; story.knows_move(n) and
//   story.has_condition(mask) booleans; story.money() 0..2^32-1 (the money as an unsigned 32-bit
//   value).
// The emitter writes each constant of a comparison in the same domain (a negative money amount as
// its unsigned 32-bit value, a stat 9 value as a signed 16-bit one), so Lua's integer comparison
// gives the original's result.
namespace dwe::formats::script {

// The version of what the emitter writes, in the first line of every module. A change to the output
// raises it and dwx's ExtractorVersion.
inline constexpr int LuaEmitterVersion = 3;

// Names a module.
struct LuaModuleName {
  std::uint16_t script =
      0; // the script id the module returns: 0 for MAPHEAD, which the game loads as script 0
  std::string source;     // where the listing came from, for the first line ("script/dg/049")
  std::string textSource; // the source of its text keys and catalog: story_<script id in 3 digits>
                          // ("story_049")
};

// A line of the module and the offset of the instruction it runs.
struct LuaLine {
  std::size_t line = 0; // from 1
  std::uint32_t offset = 0;
};

// The text of a SHOW_TEXTBOX or SET_SELECTION and its key: <textSource>.text.<offset in 4 hex
// digits>.
struct LuaText {
  std::uint32_t offset = 0;
  std::string key;
  std::string text;
};

struct LuaModule {
  std::string code;
  std::vector<LuaLine> lines; // in line order
  std::vector<LuaText> texts; // in offset order
};

// The key of the text at an offset of a script: <textSource>.text.<offset in 4 hex digits>.
std::string luaTextKey(std::string_view textSource, std::uint32_t offset);

// Writes the module of a script. The same listing and structure always give the same bytes. Fails
// when the structure names an instruction or routine the listing does not have.
core::Result<LuaModule> emitLuaModule(const ScriptListing& listing, const StructuredScript& script,
                                      const LuaModuleName& name);
// Structures a listing (structureScript) and writes its module.
core::Result<LuaModule> convertScriptToLua(const ScriptListing& listing, const LuaModuleName& name);

// The line map of a module, story/.../<name>_lines: a mapping of each line that runs an instruction
// to its offset.
core::YamlNode luaLinesToYaml(const LuaModule& module);
// The text catalog of a module, text/<language>/<textSource>: a flat mapping of text.<offset> to
// the text.
core::YamlNode luaTextsToYaml(const LuaModule& module);

} // namespace dwe::formats::script
