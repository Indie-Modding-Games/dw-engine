#include "script_warps_command.hpp"

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/script/guarded_walk.hpp>
#include <dwe/formats/script/lua_reader.hpp>
#include <dwe/formats/script/script_warps.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <print>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

#include "dwx.hpp"
#include "messages.hpp"

namespace dwe::dwx {

namespace {

namespace script = formats::script;

constexpr std::string_view StoryPrefix = "story/";
constexpr std::string_view LinesSuffix = "_lines";
constexpr std::string_view ScriptWarpsId = "field/script_warps";
constexpr std::string_view TransportSection = "bird_transport";
constexpr std::string_view DestinationsKey = "destinations";

std::string textOf(const std::vector<std::byte>& bytes) {
  std::string text(bytes.size(), '\0');
  std::ranges::transform(bytes, text.begin(), [](std::byte b) { return static_cast<char>(b); });
  return text;
}

core::Result<std::string> readText(const assets::VirtualFileSystem& files, std::string_view id) {
  auto asset = assets::AssetId::parse(id);
  if (!asset) {
    return std::unexpected{asset.error()};
  }
  auto bytes = files.read(*asset);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  return textOf(*bytes);
}

// The script number of a story module's id, story/<digits>; nothing for other story assets.
std::optional<std::uint32_t> moduleNumber(const assets::AssetId& id) {
  const std::string_view text{id.str()};
  if (!text.starts_with(StoryPrefix)) {
    return std::nullopt;
  }
  const std::string_view digits = text.substr(StoryPrefix.size());
  std::uint32_t number = 0;
  const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
  if (digits.empty() || error != std::errc{} || end != digits.data() + digits.size()) {
    return std::nullopt;
  }
  return number;
}

// An integer field of a destination, within [0, most].
std::optional<std::uint32_t> field(const core::YamlNode& node, std::string_view key,
                                   std::uint32_t most) {
  const core::YamlNode* value = node.find(key);
  if (value == nullptr || !value->isScalar()) {
    return std::nullopt;
  }
  const std::string& text = value->text();
  std::uint32_t number = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
  if (error != std::errc{} || end != text.data() + text.size() || number > most) {
    return std::nullopt;
  }
  return number;
}

// The transport menu's destinations of rules/<rules>/bird_transport (packages/session:
// BirdTransportRules reads the same list): the map, the entry it arrives at, the trigger that lists
// it and its cost. A ruleset without the file has none, and a note says so.
core::Result<std::vector<script::TransportDestination>>
readTransport(const assets::VirtualFileSystem& files, std::string_view rules,
              std::vector<std::string>& notes) {
  const std::string id = std::format("rules/{}/{}", rules, TransportSection);
  auto asset = assets::AssetId::parse(id);
  if (!asset) {
    return std::unexpected{asset.error()};
  }
  if (files.resolve(*asset) == nullptr) {
    notes.push_back(core::formatMessage(messages::ScriptWarpsNoTransport, id));
    return std::vector<script::TransportDestination>{};
  }
  auto text = readText(files, id);
  if (!text) {
    return std::unexpected{text.error()};
  }
  auto document = core::parseYaml(*text);
  if (!document) {
    return core::fail(std::format("{}: {}", id, document.error().message));
  }
  const core::YamlNode* destinations =
      document->isMapping() ? document->find(DestinationsKey) : nullptr;
  if (destinations == nullptr || !destinations->isSequence()) {
    return core::fail(core::formatMessage(messages::ScriptWarpsBadTransport, id, DestinationsKey));
  }
  std::vector<script::TransportDestination> transport;
  for (const core::YamlNode& place : destinations->items()) {
    const auto map = field(place, "map", std::numeric_limits<std::uint16_t>::max());
    const auto entry = field(place, "entry", std::numeric_limits<std::uint8_t>::max());
    const auto trigger = field(place, "trigger", std::numeric_limits<std::uint16_t>::max());
    const auto cost = field(place, "cost", std::numeric_limits<std::uint32_t>::max());
    if (!place.isMapping() || !map || !entry || !trigger || !cost) {
      return core::fail(
          core::formatMessage(messages::ScriptWarpsBadTransport, id, DestinationsKey));
    }
    transport.push_back({.map = static_cast<std::uint16_t>(*map),
                         .exit = static_cast<std::uint8_t>(*entry),
                         .trigger = static_cast<std::uint16_t>(*trigger),
                         .cost = *cost});
  }
  return transport;
}

// Every story module of the packs, read back, by the script id it returns.
core::Result<std::map<std::uint16_t, script::LuaListing>>
readModules(const assets::VirtualFileSystem& files, std::vector<std::string>& notes) {
  std::map<std::uint16_t, script::LuaListing> modules;
  std::vector<std::string> failures;
  for (const assets::AssetId& id : files.ids()) {
    if (!moduleNumber(id)) {
      continue;
    }
    auto code = readText(files, id.str());
    if (!code) {
      failures.push_back(std::format("{}: {}", id.str(), code.error().message));
      continue;
    }
    std::optional<script::LuaLineMap> lines;
    std::string linesProblem;
    const std::string linesId = id.str() + std::string{LinesSuffix};
    auto linesAsset = assets::AssetId::parse(linesId);
    if (linesAsset && files.resolve(*linesAsset) != nullptr) {
      auto text = readText(files, linesId);
      auto yaml = text ? core::parseYaml(*text)
                       : core::Result<core::YamlNode>{std::unexpected{text.error()}};
      auto map = yaml ? script::readLuaLineMap(*yaml)
                      : core::Result<script::LuaLineMap>{std::unexpected{yaml.error()}};
      if (map) {
        lines = std::move(*map);
      } else {
        linesProblem = map.error().message;
      }
    } else {
      linesProblem = core::formatMessage(messages::ScriptWarpsNoLineMap, linesId);
    }
    auto module = script::readLuaModule(*code, lines ? &*lines : nullptr);
    if (!module) {
      failures.push_back(std::format("{}: {}", id.str(), module.error().message));
      continue;
    }
    if (module->lineMapProblem) {
      linesProblem = *module->lineMapProblem;
    }
    if (!module->offsets) {
      notes.push_back(core::formatMessage(messages::ScriptWarpsNoOffsets, id.str(), linesProblem));
    }
    const std::uint16_t number = module->script;
    if (!modules.emplace(number, std::move(*module)).second) {
      failures.push_back(core::formatMessage(messages::ScriptWarpsScriptTwice, id.str(), number));
    }
  }
  if (!failures.empty()) {
    std::string text;
    for (const std::string& failure : failures) {
      text += "\n  " + failure;
    }
    return core::fail(core::formatMessage(messages::ScriptWarpsUnreadable, text));
  }
  return modules;
}

// Where two documents first differ: the line number and both lines.
std::string firstDifference(std::string_view packed, std::string_view derived) {
  std::size_t line = 1;
  while (true) {
    const std::size_t packedEnd = packed.find('\n');
    const std::size_t derivedEnd = derived.find('\n');
    const std::string_view packedLine = packed.substr(0, packedEnd);
    const std::string_view derivedLine = derived.substr(0, derivedEnd);
    if (packedLine != derivedLine ||
        (packedEnd == std::string_view::npos) != (derivedEnd == std::string_view::npos)) {
      return core::formatMessage(messages::ScriptWarpsDifferAt, line, packedLine, derivedLine);
    }
    if (packedEnd == std::string_view::npos) {
      return {};
    }
    packed.remove_prefix(packedEnd + 1);
    derived.remove_prefix(derivedEnd + 1);
    ++line;
  }
}

// The packs of some folders, mounted in that order. The files read through the archives, which
// live as long as this, and through the disk, which lives as long as the program.
struct MountedPacks {
  assets::OpenedPacks opened;
  assets::VirtualFileSystem files;
};

core::Result<MountedPacks> mountFolders(std::span<const std::filesystem::path> folders) {
  static const assets::DiskFileSystem disk;
  std::vector<assets::FoundPack> found;
  for (const std::filesystem::path& folder : folders) {
    auto packs = assets::findPacks(disk, folder);
    if (!packs) {
      return std::unexpected{packs.error()};
    }
    found.insert(found.end(), packs->packs.begin(), packs->packs.end());
  }
  auto opened = assets::openPacks(disk, found, assets::PackOrigin::Engine);
  if (!opened) {
    return std::unexpected{opened.error()};
  }
  auto files = assets::VirtualFileSystem::mount(opened->locations);
  if (!files) {
    return std::unexpected{files.error()};
  }
  return MountedPacks{.opened = std::move(*opened), .files = std::move(*files)};
}

} // namespace

core::Result<DerivedScriptWarps> deriveScriptWarps(const assets::VirtualFileSystem& files,
                                                   std::string_view rules) {
  DerivedScriptWarps derived;
  auto modules = readModules(files, derived.notes);
  if (!modules) {
    return std::unexpected{modules.error()};
  }
  derived.modules = modules->size();
  auto table = assets::loadMapTable(files);
  if (!table) {
    return std::unexpected{table.error()};
  }
  auto transport = readTransport(files, rules, derived.notes);
  if (!transport) {
    return std::unexpected{transport.error()};
  }
  // The sections NPCs run, by map: the script ids of each map's NPCs, as dwx extract reads them
  // from the map files. A map whose document does not read has none.
  std::set<std::pair<std::uint16_t, std::uint16_t>> talks;
  std::string unreadMaps;
  for (const assets::MapEntry& entry : table->maps) {
    if (!entry.hasFile()) {
      continue;
    }
    const auto document = assets::loadMapDocument(files, entry.id);
    if (!document) {
      unreadMaps += std::format("\n  {}: {}", entry.id, document.error().message);
      continue;
    }
    for (const assets::MapNpc& npc : document->npcs) {
      if (npc.scriptId >= 0) {
        talks.emplace(static_cast<std::uint16_t>(entry.index),
                      static_cast<std::uint16_t>(npc.scriptId));
      }
    }
  }
  if (!unreadMaps.empty()) {
    derived.notes.push_back(core::formatMessage(messages::ScriptWarpsUnreadMaps, unreadMaps));
  }
  const script::ScriptLookup lookup =
      [&modules](std::uint16_t id) -> std::optional<script::WalkableScript> {
    const auto found = modules->find(id);
    if (found == modules->end()) {
      return std::nullopt;
    }
    return script::WalkableScript{.listing = &found->second.listing,
                                  .structured = &found->second.structured,
                                  .offsets = found->second.offsets};
  };
  const script::MapName mapName = [&table](std::uint16_t map) -> std::optional<std::string> {
    const assets::MapEntry* entry = table->findByIndex(map);
    if (entry == nullptr) {
      return std::nullopt;
    }
    return entry->id;
  };
  const script::ScriptWarpSources sources{
      .talks = [&talks](std::uint16_t map,
                        std::uint16_t section) { return talks.contains({map, section}); },
      .transport = std::move(*transport)};
  const script::ScriptWarps warps = script::findScriptWarps(lookup, sources);
  derived.warps = warps.warps.size();
  auto text = core::emitYaml(script::scriptWarpsToYaml(warps, lookup, mapName));
  if (!text) {
    return std::unexpected{text.error()};
  }
  derived.yaml = std::move(*text);
  return derived;
}

core::Result<DerivedScriptWarps> deriveScriptWarps(std::span<const std::filesystem::path> folders,
                                                   std::string_view rules) {
  auto mounted = mountFolders(folders);
  if (!mounted) {
    return std::unexpected{mounted.error()};
  }
  return deriveScriptWarps(mounted->files, rules);
}

int scriptWarpsCommand(std::span<const std::filesystem::path> folders,
                       const std::optional<std::filesystem::path>& out, std::string_view rules) {
  auto mounted = mountFolders(folders);
  auto derived = mounted ? deriveScriptWarps(mounted->files, rules)
                         : core::Result<DerivedScriptWarps>{std::unexpected{mounted.error()}};
  if (!derived) {
    std::println(stderr, "dwx: {}", derived.error().message);
    return ExitFailed;
  }
  for (const std::string& note : derived->notes) {
    std::println(stderr, "note: {}", note);
  }
  if (out) {
    std::ofstream file{*out, std::ios::binary};
    file << derived->yaml;
    file.close();
    if (!file) {
      std::println(stderr, "dwx: {}",
                   core::formatMessage(messages::ScriptWarpsCannotWrite, assets::pathText(*out)));
      return ExitFailed;
    }
    std::println("{}", core::formatMessage(messages::ScriptWarpsWritten, assets::pathText(*out),
                                           derived->warps, derived->modules));
    return ExitOk;
  }
  auto packed = readText(mounted->files, ScriptWarpsId);
  if (!packed) {
    std::println(stderr, "dwx: {}: {}", ScriptWarpsId, packed.error().message);
    return ExitFailed;
  }
  const std::string difference = firstDifference(*packed, derived->yaml);
  if (difference.empty()) {
    std::println("{}", core::formatMessage(messages::ScriptWarpsMatch, ScriptWarpsId,
                                           derived->warps, derived->modules));
    return ExitOk;
  }
  std::println("{}", core::formatMessage(messages::ScriptWarpsDiffer, ScriptWarpsId, difference));
  return ExitUnknown;
}

} // namespace dwe::dwx
