#include "script_maps.hpp"

#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/script/scn_file.hpp>

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string_view>

namespace dwe::dwx {

namespace {

constexpr std::string_view MapHeadPath = "SCN/MAPHEAD.SCN";
constexpr std::string_view ScriptPackPath = "SCN/DG.SCN";

std::optional<std::int32_t> argument(const formats::script::Instruction& instruction,
                                     std::string_view name) {
  const auto found =
      std::ranges::find(instruction.arguments, name, &formats::script::Argument::name);
  if (found == instruction.arguments.end()) {
    return std::nullopt;
  }
  return found->value;
}

core::Result<std::vector<std::byte>> readFile(const Disc& disc, std::string_view path) {
  const formats::disc::IsoFile* file = disc.findFile(path);
  if (file == nullptr) {
    return core::fail(std::format("{} is not on the disc", path));
  }
  return formats::disc::readIsoFile(disc.source(), *file);
}

core::Result<ScriptCode> disassemble(std::uint16_t id, std::span<const std::byte> bytes) {
  auto code = formats::script::disassembleScript(bytes);
  if (!code) {
    return core::fail(std::format("script {}: {}", id, code.error().message));
  }
  return ScriptCode{.id = id, .code = std::move(*code)};
}

} // namespace

core::Result<std::vector<ScriptCode>> readScripts(const Disc& disc) {
  const auto mapHead = readFile(disc, MapHeadPath);
  if (!mapHead) {
    return std::unexpected{mapHead.error()};
  }
  const auto pack = readFile(disc, ScriptPackPath);
  if (!pack) {
    return std::unexpected{pack.error()};
  }
  const auto slots = formats::script::readScriptTable(*pack);
  if (!slots) {
    return std::unexpected{slots.error()};
  }
  std::vector<ScriptCode> scripts;
  scripts.reserve(slots->size());
  // getScript(0) returns the resident MAPHEAD.SCN, never the copy in DG.SCN.
  for (std::size_t id = 0; id < slots->size(); ++id) {
    const auto bytes = id == 0 ? std::span<const std::byte>{*mapHead}
                               : std::span(*pack).subspan((*slots)[id].offset, (*slots)[id].size);
    auto script = disassemble(static_cast<std::uint16_t>(id), bytes);
    if (!script) {
      return std::unexpected{script.error()};
    }
    scripts.push_back(std::move(*script));
  }
  return scripts;
}

RoutineMaps findRoutineMaps(std::span<const ScriptCode> scripts, std::uint8_t routine) {
  std::map<std::uint16_t, std::set<std::uint16_t>> mapsOf;
  std::map<std::uint16_t, std::set<std::uint16_t>> jumpsInto;
  std::set<std::uint16_t> callers;
  for (const ScriptCode& script : scripts) {
    for (const auto& instruction : script.code.instructions) {
      const std::string_view op = formats::script::opcodeName(instruction.opcode);
      if (op == "set_script") {
        const auto target = argument(instruction, "script");
        const auto map = argument(instruction, "map");
        if (target && map) {
          mapsOf[static_cast<std::uint16_t>(*target)].insert(static_cast<std::uint16_t>(*map));
        }
      } else if (op == "jump_to_file" || op == "jump_to_file_and_link") {
        if (const auto target = argument(instruction, "script"); target) {
          jumpsInto[static_cast<std::uint16_t>(*target)].insert(script.id);
        }
      } else if (op == "call_routine" && argument(instruction, "routine") == routine) {
        callers.insert(script.id);
      }
    }
  }

  RoutineMaps result;
  std::set<std::uint16_t> maps;
  for (const std::uint16_t caller : callers) {
    // The scripts whose code runs the caller's: itself and every script that jumps into it,
    // transitively.
    std::set<std::uint16_t> runners{caller};
    std::vector<std::uint16_t> pending{caller};
    while (!pending.empty()) {
      const std::uint16_t script = pending.back();
      pending.pop_back();
      for (const std::uint16_t from : jumpsInto[script]) {
        if (runners.insert(from).second) {
          pending.push_back(from);
        }
      }
    }
    bool loaded = false;
    for (const std::uint16_t runner : runners) {
      loaded = loaded || !mapsOf[runner].empty();
      maps.insert(mapsOf[runner].begin(), mapsOf[runner].end());
    }
    if (!loaded) {
      result.scriptsWithoutMap.push_back(caller);
    }
  }
  result.maps.assign(maps.begin(), maps.end());
  return result;
}

} // namespace dwe::dwx
