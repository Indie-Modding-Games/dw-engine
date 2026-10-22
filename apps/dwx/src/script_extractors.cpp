#include "script_extractors.hpp"

#include <dwe/core/yaml.hpp>
#include <dwe/formats/map/map_file.hpp>
#include <dwe/formats/script/disassembly.hpp>
#include <dwe/formats/script/lua_emitter.hpp>
#include <dwe/formats/script/new_game_stats.hpp>
#include <dwe/formats/script/scn_file.hpp>
#include <dwe/formats/script/script_listing.hpp>
#include <dwe/formats/script/script_warps.hpp>
#include <dwe/formats/script/script_yaml.hpp>
#include <dwe/formats/script/structuring.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "asset_names.hpp"
#include "map_extractors.hpp"
#include "story_conversion.hpp"

namespace dwe::dwx {

namespace {

constexpr std::string_view MapHeadPath = "SCN/MAPHEAD.SCN";
constexpr std::string_view ScriptPackPath = "SCN/DG.SCN";
// data/new_game_stats: what the new-game script sets.
constexpr std::string_view NewGameStatsDocument = "new_game_stats";
// field/script_warps: the map changes of the event scripts; data/new_game_story: the triggers a new game sets.
constexpr std::string_view ScriptWarpsDocument = "script_warps";
constexpr std::string_view NewGameStoryDocument = "new_game_story";
// The language of the story's text catalogs: the script texts decode with the US release's charset
// (text::decodeScriptText).
constexpr std::string_view StoryLanguage = "en";

// What the listings of a file add up to, for the notes.
struct Totals {
    std::size_t scripts = 0;
    std::size_t instructions = 0;
    std::size_t texts = 0;
    std::size_t unreachable = 0;
    std::size_t padding = 0; // bytes after the end of the code
    std::size_t modules = 0; // Lua modules
    std::size_t luaBytes = 0;
};

core::Result<OutputFile> yamlOutput(const core::Result<assets::AssetId>& id, const core::YamlNode& node) {
    if (!id) {
        return std::unexpected{id.error()};
    }
    const auto text = core::emitYaml(node);
    if (!text) {
        return std::unexpected{text.error()};
    }
    const auto bytes = std::as_bytes(std::span{*text});
    return OutputFile{.id = *id, .extension = "yaml", .bytes = std::vector<std::byte>(bytes.begin(), bytes.end())};
}

// The Lua module of a script (formats::script::convertScriptToLua), its line map and its English texts: story/<n>,
// story/<n>_lines and text/en/story_<n>. A script that does not convert is an unknown; its listing is still written.
core::Result<void> addStory(Extraction& extraction, Totals& totals, std::optional<std::size_t> id,
                            const formats::script::Disassembly& disassembly) {
    const formats::script::LuaModuleName name = storyModuleName(id);
    const auto module = formats::script::convertScriptToLua(formats::script::listingOf(disassembly), name);
    if (!module) {
        extraction.unknowns.push_back(std::format("{}: no Lua module: {}", name.source, module.error().message));
        return {};
    }
    const auto moduleId = names::storyModule(name.script);
    if (!moduleId) {
        return std::unexpected{moduleId.error()};
    }
    const auto code = std::as_bytes(std::span{module->code});
    extraction.outputs.push_back(
        OutputFile{.id = *moduleId, .extension = "lua", .bytes = std::vector<std::byte>(code.begin(), code.end())});
    auto lines = yamlOutput(names::storyLines(name.script), formats::script::luaLinesToYaml(*module));
    if (!lines) {
        return std::unexpected{lines.error()};
    }
    extraction.outputs.push_back(std::move(*lines));
    if (!module->texts.empty()) {
        auto texts =
            yamlOutput(names::textTable(StoryLanguage, name.textSource), formats::script::luaTextsToYaml(*module));
        if (!texts) {
            return std::unexpected{texts.error()};
        }
        extraction.outputs.push_back(std::move(*texts));
    }
    ++totals.modules;
    totals.luaBytes += module->code.size();
    return {};
}

// Disassembles one script and adds its listing. Every byte up to the end of the code must decode; what follows it
// is the padding of the script's sectors, which nothing reads.
core::Result<void> addScript(Extraction& extraction, Totals& totals, std::string_view volumePath,
                             std::optional<std::size_t> id, std::span<const std::byte> script) {
    const std::string name = id ? std::format("script {}", *id) : std::string{"the script"};
    auto disassembly = formats::script::disassembleScript(script);
    if (!disassembly) {
        extraction.unknowns.push_back(std::format("{}: {}", name, disassembly.error().message));
        return {};
    }
    for (const std::string& unknown : disassembly->unknowns) {
        extraction.unknowns.push_back(std::format("{}: {}", name, unknown));
    }
    auto output = yamlOutput(names::scriptListing(volumePath, id), formats::script::scriptToYaml(*disassembly));
    if (!output) {
        return std::unexpected{output.error()};
    }
    extraction.outputs.push_back(std::move(*output));
    if (auto added = addStory(extraction, totals, id, *disassembly); !added) {
        return added;
    }
    ++totals.scripts;
    totals.instructions += disassembly->instructions.size();
    totals.texts += static_cast<std::size_t>(std::ranges::count_if(
        disassembly->instructions, [](const formats::script::Instruction& i) { return i.text.has_value(); }));
    totals.unreachable += disassembly->unreachable;
    totals.padding += script.size() - std::min<std::size_t>(disassembly->codeEnd, script.size());
    return {};
}

// The partner's stats at the start of a new game, which the new-game script sets (formats::script::findNewGameStats):
// data/new_game_stats. A script without them is noted and writes nothing.
core::Result<void> addNewGameStats(Extraction& extraction, const Disc& disc, std::span<const std::byte> script) {
    const auto disassembly = formats::script::disassembleScript(script);
    if (!disassembly) {
        return {};
    }
    const auto stats = formats::script::findNewGameStats(*disassembly);
    if (!stats) {
        extraction.notes.push_back(
            std::format("script {} does not set the new game's stats ({}); data/{} is not written",
                        formats::script::NewGameScript, stats.error().message, NewGameStatsDocument));
        return {};
    }
    const std::string partner = names::digimonSlug(disc.tables(), static_cast<std::size_t>(stats->partnerType));
    auto output =
        yamlOutput(names::tableDocument(NewGameStatsDocument), formats::script::newGameStatsToYaml(*stats, partner));
    if (!output) {
        return std::unexpected{output.error()};
    }
    extraction.outputs.push_back(std::move(*output));
    return {};
}

// The opening of a new game (formats::script::findNewGameOpening): the map change the cell under the first map's
// spawn makes. Noted when the map cannot be read or its cell makes none.
void addNewGameOpening(Extraction& extraction, const Disc& disc, formats::script::NewGameTriggers& newGame,
                       const formats::script::ScriptLookup& lookup, const formats::script::MapName& mapName) {
    const std::string map = mapName(newGame.map).value_or(std::to_string(newGame.map));
    const auto file = readMapOf(disc, newGame.map);
    if (!file) {
        extraction.notes.push_back(std::format("the first map {} cannot be read ({}), so data/{} has no opening", map,
                                               file.error().message, NewGameStoryDocument));
        return;
    }
    const auto cell = formats::map::collisionUnderSpawn(*file, formats::script::NewGameExit);
    if (!cell) {
        extraction.notes.push_back(std::format("a new game's spawn on {} at exit {} is on no cell, so data/{} has no "
                                               "opening",
                                               map, formats::script::NewGameExit, NewGameStoryDocument));
        return;
    }
    auto found = formats::script::findNewGameOpening(newGame, *cell, lookup);
    if (!found) {
        extraction.notes.push_back(std::format("a new game's spawn on {} at exit {} makes no certain map change ({}), "
                                               "so data/{} has no opening",
                                               map, formats::script::NewGameExit, found.error().message,
                                               NewGameStoryDocument));
        return;
    }
    newGame.opening = std::move(*found);
    const formats::script::NewGameOpening& opening = *newGame.opening;
    extraction.notes.push_back(std::format("a new game's spawn on {} at exit {} is on step cell {}, which goes to {} "
                                           "exit {} at once and sets {} triggers: data/{} keeps it as the opening",
                                           map, opening.exit, opening.section,
                                           mapName(opening.screen).value_or(std::to_string(opening.screen)),
                                           opening.screenExit, opening.sets.size(), NewGameStoryDocument));
}

// A script listed and structured, for the walks of formats::script.
struct WalkedScript {
    formats::script::ScriptListing listing;
    formats::script::StructuredScript structured;
};

// The map changes of every map's scripts (formats::script::findScriptWarps): field/script_warps, and the triggers a
// new game sets: data/new_game_story. `scripts` holds every script of the pack, MAPHEAD as script 0; one that does
// not structure is noted and left out. Without the executable's tables the maps have no ids, and nothing is written.
core::Result<void> addScriptWarps(Extraction& extraction, const Disc& disc,
                                  const std::map<std::uint16_t, std::span<const std::byte>>& scripts) {
    std::map<std::uint16_t, WalkedScript> walked;
    for (const auto& [id, bytes] : scripts) {
        const auto disassembly = formats::script::disassembleScript(bytes);
        if (!disassembly) {
            continue;
        }
        auto listing = formats::script::listingOf(*disassembly);
        auto structured = formats::script::structureScript(listing);
        if (!structured) {
            extraction.notes.push_back(std::format("script {} does not structure ({}); its map changes are left out",
                                                   id, structured.error().message));
            continue;
        }
        walked.emplace(id, WalkedScript{.listing = std::move(listing), .structured = std::move(*structured)});
    }
    const GameTables* tables = disc.tables();
    if (tables == nullptr) {
        extraction.notes.push_back(std::format("the executable's tables are unknown, so field/{} and data/{} are not "
                                               "written",
                                               ScriptWarpsDocument, NewGameStoryDocument));
        return {};
    }
    const formats::script::ScriptLookup lookup =
        [&walked](std::uint16_t id) -> std::optional<formats::script::WalkableScript> {
        const auto found = walked.find(id);
        if (found == walked.end()) {
            return std::nullopt;
        }
        return formats::script::WalkableScript{.listing = &found->second.listing,
                                               .structured = &found->second.structured};
    };
    const formats::script::MapName mapName = [tables](std::uint16_t map) -> std::optional<std::string> {
        if (map >= tables->ids.maps.size() || tables->ids.maps[map].empty()) {
            return std::nullopt;
        }
        return tables->ids.maps[map];
    };
    // The sections NPCs run, by map: the script ids of each map's NPCs (overworld.c: loadMapDigimon). A map whose file
    // cannot be read has none.
    std::set<std::pair<std::uint16_t, std::uint16_t>> talks;
    std::size_t unreadMaps = 0;
    for (std::size_t map = 0; map < tables->tables.mapEntries.size(); ++map) {
        if (tables->tables.mapEntries[map].filename.empty()) {
            continue;
        }
        const auto file = readMapOf(disc, map);
        if (!file) {
            ++unreadMaps;
            continue;
        }
        for (const formats::map::MapNpc& npc : file->entities.npcs) {
            if (npc.scriptId >= 0) {
                talks.emplace(static_cast<std::uint16_t>(map), static_cast<std::uint16_t>(npc.scriptId));
            }
        }
    }
    if (unreadMaps != 0) {
        extraction.notes.push_back(std::format("{} maps cannot be read, so their NPCs' sections are not told apart "
                                               "from other sections in field/{}",
                                               unreadMaps, ScriptWarpsDocument));
    }
    formats::script::ScriptWarpSources sources{
        .talks = [&talks](std::uint16_t map, std::uint16_t section) { return talks.contains({map, section}); },
        .transport = {}};
    for (const formats::exe::TransportTarget& target : tables->tables.transportTargets) {
        sources.transport.push_back(
            {.map = target.mapId, .exit = target.exit, .trigger = target.trigger, .cost = target.cost});
    }
    const auto warps = formats::script::findScriptWarps(lookup, sources);
    auto output = yamlOutput(names::fieldDocument(ScriptWarpsDocument),
                             formats::script::scriptWarpsToYaml(warps, lookup, mapName));
    if (!output) {
        return std::unexpected{output.error()};
    }
    extraction.outputs.push_back(std::move(*output));
    std::array<std::size_t, 5> byVia{};
    std::size_t transportPicks = 0;
    std::size_t unguarded = 0;
    std::size_t teleports = 0;
    std::size_t unnamed = 0;
    for (const formats::script::ScriptWarp& warp : warps.warps) {
        ++byVia.at(static_cast<std::size_t>(warp.via));
        unguarded += warp.scriptGuard.terms.empty() && !warp.scriptGuard.unstructured &&
                             warp.change.path.guard.terms.empty() && !warp.change.path.guard.unstructured
                         ? 1
                         : 0;
        teleports += warp.change.screenVariable ? 1 : 0;
        transportPicks += warp.transport ? 1 : 0;
        unnamed += warp.change.screen && !mapName(static_cast<std::uint16_t>(*warp.change.screen)) ? 1 : 0;
    }
    extraction.notes.push_back(
        std::format("{} map changes in the maps' scripts: {} on stepping on a cell, {} on "
                    "confirm, {} on entering, {} on talking to an NPC, {} from other sections; {} "
                    "without a condition, {} teleports (the map is in a variable), {} to a "
                    "screen no map has; {} are picks of the transport menu ({} entries)",
                    warps.warps.size(), byVia[0], byVia[1], byVia[2], byVia[4], byVia[3], unguarded, teleports, unnamed,
                    transportPicks, sources.transport.size()));
    extraction.notes.push_back(std::format("{} MAPHEAD sections load no map's script (new game, death, transport...); "
                                           "their map changes are not in field/{}",
                                           warps.otherMapHeadSections.size(), ScriptWarpsDocument));
    auto newGame = formats::script::findNewGameTriggers(lookup);
    if (!newGame) {
        extraction.notes.push_back(
            std::format("MAPHEAD has no new-game section; data/{} is not written", NewGameStoryDocument));
        return {};
    }
    if (!newGame->agreed) {
        extraction.notes.push_back(std::format("the paths of a new game end with different triggers set; data/{} "
                                               "keeps the ones they share",
                                               NewGameStoryDocument));
    }
    addNewGameOpening(extraction, disc, *newGame, lookup, mapName);
    auto story = yamlOutput(names::tableDocument(NewGameStoryDocument),
                            formats::script::newGameTriggersToYaml(*newGame, mapName));
    if (!story) {
        return std::unexpected{story.error()};
    }
    extraction.outputs.push_back(std::move(*story));
    return {};
}

void noteTotals(Extraction& extraction, const Totals& totals) {
    extraction.notes.push_back(
        std::format("{} scripts, {} instructions, {} texts", totals.scripts, totals.instructions, totals.texts));
    extraction.notes.push_back(std::format("{} Lua modules in story/, {} bytes", totals.modules, totals.luaBytes));
    extraction.notes.push_back(std::format("{} instructions are not reached from any section; they are kept in the "
                                           "listings (most are an unreachable jump after an END_SECTION)",
                                           totals.unreachable));
    if (totals.padding != 0) {
        extraction.notes.push_back(std::format("{} bytes after the end of the scripts' code are the padding of their "
                                               "sectors: leftovers that nothing reads",
                                               totals.padding));
    }
}

core::Result<Extraction> extractScriptPack(const Disc& disc, std::span<const std::byte> file) {
    auto slots = formats::script::readScriptTable(file);
    if (!slots) {
        return std::unexpected{slots.error()};
    }
    Extraction extraction;
    Totals totals;
    const std::size_t tableEnd = (slots->size() + 1) * sizeof(std::uint32_t);
    const std::size_t tableBytes = std::min<std::size_t>(formats::script::ScriptTableSize, slots->front().offset);
    if (tableEnd < tableBytes) {
        const auto rest = file.subspan(tableEnd, tableBytes - tableEnd);
        const auto leftovers = std::ranges::count_if(rest, [](std::byte b) { return b != std::byte{0}; });
        if (leftovers != 0) {
            extraction.notes.push_back(std::format("the table holds {} script offsets; {} of the {} bytes after them "
                                                   "up to the first script are leftovers that nothing reads",
                                                   slots->size(), leftovers, rest.size()));
        }
    }
    // initializeScripts loads MAPHEAD.SCN as script 0, so the copy in this file is never read.
    std::optional<std::vector<std::byte>> mapHead;
    if (const formats::disc::IsoFile* mapHeadFile = disc.findFile(MapHeadPath); mapHeadFile != nullptr) {
        auto bytes = formats::disc::readIsoFile(disc.source(), *mapHeadFile);
        if (!bytes) {
            return std::unexpected{bytes.error()};
        }
        mapHead = std::move(*bytes);
    }
    std::map<std::uint16_t, std::span<const std::byte>> scripts;
    for (std::size_t id = 0; id < slots->size(); ++id) {
        const auto script = file.subspan((*slots)[id].offset, (*slots)[id].size);
        scripts[static_cast<std::uint16_t>(id)] = id == 0 && mapHead ? std::span<const std::byte>{*mapHead} : script;
        // Both would be story/000. A slot runs up to the next script, so the copy is followed by its slot's padding.
        if (id == 0 && mapHead) {
            if (script.size() < mapHead->size() || !std::ranges::equal(script.first(mapHead->size()), *mapHead)) {
                return core::fail(std::format("script 0 is not a copy of {}: both would be script 0 of the story, "
                                              "and the game only loads {}",
                                              MapHeadPath, MapHeadPath));
            }
            extraction.notes.push_back(std::format("script 0 is a copy of {}, which the game loads instead; it is "
                                                   "extracted from that file",
                                                   MapHeadPath));
            continue;
        }
        if (auto added = addScript(extraction, totals, ScriptPackPath, id, script); !added) {
            return std::unexpected{added.error()};
        }
        if (id == formats::script::NewGameScript) {
            if (auto added = addNewGameStats(extraction, disc, script); !added) {
                return std::unexpected{added.error()};
            }
        }
    }
    if (auto added = addScriptWarps(extraction, disc, scripts); !added) {
        return std::unexpected{added.error()};
    }
    noteTotals(extraction, totals);
    return extraction;
}

} // namespace

bool handlesScriptFile(std::string_view path) {
    return path == MapHeadPath || path == ScriptPackPath;
}

core::Result<Extraction> extractScriptFile(const Disc& disc, const formats::disc::IsoFile& file) {
    const auto bytes = formats::disc::readIsoFile(disc.source(), file);
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }
    if (file.path == ScriptPackPath) {
        return extractScriptPack(disc, *bytes);
    }
    Extraction extraction;
    Totals totals;
    if (auto added = addScript(extraction, totals, file.path, std::nullopt, *bytes); !added) {
        return std::unexpected{added.error()};
    }
    noteTotals(extraction, totals);
    return extraction;
}

} // namespace dwe::dwx
