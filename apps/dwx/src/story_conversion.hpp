#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/script/lua_emitter.hpp>
#include <dwe/formats/script/script_listing.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// The Lua of the event scripts (formats::script::convertScriptToLua): what dwx extract writes next
// to each listing, and dwx convert-script for a listing on its own, such as one a mod edited.
namespace dwe::dwx {

// The name of the module of a script, by its script id: 0 is MAPHEAD (from script/maphead), any
// other one a script of DG.SCN (from script/dg/049). Its texts are story_<id in 3 digits>
// (story_000, story_049).
formats::script::LuaModuleName storyModuleName(std::optional<std::size_t> script);

// dwx convert-script: converts the listing at a file (script/dg/<n>.yaml or script/maphead.yaml,
// named so; n from 1 to 65535) and writes <id>.lua, <id>_lines.yaml and <id>_texts.yaml into a
// folder, with <id> the script id in 3 digits as in story/<id>. Returns ExitOk, or ExitFailed with
// the reason on the standard error.
int convertScript(const std::filesystem::path& listing, const std::filesystem::path& out);

} // namespace dwe::dwx
