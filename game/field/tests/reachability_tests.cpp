#include <dwe/assets/file_system.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/assets/new_game_story.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/collision_triggers.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/field/rules.hpp>
#include <dwe/game/field/script_warps.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

#include "content_fixture.hpp"
#include "messages.hpp"

// Which maps a player can get to from the start of a new game, and back, through the warps the field takes: the
// maps' warp tables and the scripts' map changes on stepping on a cell and on confirming on one
// (field/script_warps). docs/reference/campo.md, "Alcance de los mapas", has the counts and the exceptions.

using dwe::game::field::testing::mountContent;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
    return result ? std::string{} : result.error().message;
}

// Measured on the US disc with dwx's field/script_warps (docs/reference/campo.md, "Alcance de los mapas"). A new
// game reaches few maps, all with a way back: the way on from mayo00 waits for trigger 203, which an NPC there sets.
// The opening's map is not among them: the field starts after the opening, with the triggers it sets.
constexpr std::array<std::string_view, 5> ExpectedNewGameMaps{"mayo00", "room08", "trai00", "twna01", "twnb01"};
constexpr std::size_t ExpectedOpenReached = 236;
constexpr std::size_t ExpectedOpenWithWayBack = 233;
// What the vanilla policy decides on the disc (docs/architecture/warps-de-script.md): no script warp goes through a
// recursion the extraction cut, and on the maps a new game reaches none is taken only by the policy nor hidden by an
// unstructured path. A change here changes what a new game can reach: report it before changing the number.
constexpr std::size_t ExpectedWarpsWithRecursion = 0;
// What runs the script warps of extractor version 5 (docs/architecture/warps-de-script.md): every section that is
// not a cell's or an entry's is an NPC's, and the transport routine's 6 calls give one pick per table entry each.
constexpr std::size_t ExpectedTalkWarps = 100;
constexpr std::size_t ExpectedOtherWarps = 0;
constexpr std::size_t ExpectedTransportPicks = 36;
constexpr std::size_t ExpectedAllowedInNewGame = 0;
constexpr std::size_t ExpectedUnstructuredInNewGame = 0;
// The warps of the maps a new game reaches that the vanilla policy takes only because game code on their path
// (call_routine, call_digimon_routine, give_item, compare_date) may have written a trigger they test. Measured
// 2026-10-22: none. The disc has 37 warps with a forget term; the two on these maps (twna01 and room08, script 209,
// forget at 0x8a0) run from an NPC's section, which the field does not take.
constexpr std::size_t ExpectedForgetDependentInNewGame = 0;
// The cells and sections whose warps the vanilla policy takes from a new game go to more than one place, with none
// taken without the policy (the field takes the first and logs it): on every map, and on the maps a new game reaches.
// Measured 2026-10-22: none on the disc.
constexpr std::size_t ExpectedAmbiguousSources = 0;
constexpr std::size_t ExpectedAmbiguousInNewGame = 0;

// The maps that every warp of the scripts, whatever its condition, does not join to the rest.
enum class Exception : std::uint8_t { Unreached, WithoutWayBack };

struct MapException {
    std::string_view map;
    Exception kind;
    std::string_view reason;
};

constexpr std::array ExpectedExceptions{
    MapException{.map = "mgen14", .kind = Exception::Unreached, .reason = "unused (map.c: changeMap callers)"},
    MapException{.map = "mgen15", .kind = Exception::Unreached, .reason = "unused (map.c: changeMap callers)"},
    MapException{.map = "mgen16", .kind = Exception::Unreached, .reason = "unused (map.c: changeMap callers)"},
    MapException{.map = "miha03",
                 .kind = Exception::Unreached,
                 .reason = "no known way (map.c: changeMap callers; script 49's call_routine 0x15 is "
                           "resetMapObjectAnimation, and the transport goes only to script_common.c: "
                           "BIRDRA_TRANSPORT_TARGETS 38, 70, 79, 93, 105, 119)"},
    MapException{.map = "yaka26", .kind = Exception::Unreached, .reason = "talk warp (script 63, section 5)"},
    MapException{.map = "icsa08", .kind = Exception::WithoutWayBack, .reason = "no warp leaves it (script 187)"},
    MapException{.map = "tend01", .kind = Exception::WithoutWayBack, .reason = "the ending (script 184)"},
    MapException{.map = "tend02", .kind = Exception::WithoutWayBack, .reason = "the ending (script 184)"},
};

using Graph = std::map<std::string, std::set<std::string>>;

std::set<std::string> reach(const Graph& graph, const std::string& from) {
    std::set<std::string> seen{from};
    std::vector<std::string> pending{from};
    while (!pending.empty()) {
        const std::string map = pending.back();
        pending.pop_back();
        const auto next = graph.find(map);
        if (next == graph.end()) {
            continue;
        }
        for (const std::string& target : next->second) {
            if (seen.insert(target).second) {
                pending.push_back(target);
            }
        }
    }
    return seen;
}

Graph reversed(const Graph& graph) {
    Graph back;
    for (const auto& [from, targets] : graph) {
        for (const std::string& target : targets) {
            back[target].insert(from);
        }
    }
    return back;
}

struct Reach {
    std::set<std::string> reached;
    std::set<std::string> withWayBack;
};

bool hasRecursion(const dwe::game::field::WarpGuard& guard) {
    return std::ranges::any_of(
        guard.terms, [](const auto& term) { return std::holds_alternative<dwe::game::field::GuardRecursion>(term); });
}

bool hasRecursion(const dwe::game::field::ScriptWarp& warp) {
    return hasRecursion(warp.scriptGuard) || hasRecursion(warp.guard);
}

// The warps by map, cell and source, for a test's message.
std::string sourcesOf(const std::vector<const dwe::game::field::ScriptWarp*>& warps) {
    std::string text;
    for (const auto* warp : warps) {
        text += std::format("\n  {} cell {} -> {}: {}", warp->map, warp->section, warp->targetMap.value_or("?"),
                            warp->source);
    }
    return text;
}

Reach reachFrom(const Graph& graph, const std::string& start) {
    Reach result{.reached = reach(graph, start), .withWayBack = {}};
    const std::set<std::string> back = reach(reversed(graph), start);
    std::ranges::set_intersection(result.reached, back, std::inserter(result.withWayBack, result.withWayBack.begin()));
    return result;
}

} // namespace

TEST_CASE("the user's disc, extracted: a new game reaches its maps and back, and the scripts join all but a few") {
    const dwe::assets::DiskFileSystem disk;
    const auto vfs = mountContent(disk);
    if (!vfs) {
        MESSAGE("DWE_CONTENT is not set: skipping the reachability of extracted content");
        return;
    }
    const auto table = dwe::assets::loadMapTable(*vfs);
    const auto warps = dwe::game::field::loadFieldWarps(*vfs);
    auto scriptWarps = dwe::game::field::loadScriptWarps(*vfs);
    const auto story = dwe::assets::loadNewGameStory(*vfs);
    REQUIRE_MESSAGE(table.has_value(), errorOf(table));
    REQUIRE_MESSAGE(warps.has_value(), errorOf(warps));
    REQUIRE_MESSAGE(scriptWarps.has_value(), errorOf(scriptWarps));
    REQUIRE_MESSAGE(story.has_value(), errorOf(story));
    REQUIRE_MESSAGE((!scriptWarps->empty() && story->has_value()),
                    "the content has no field/script_warps or data/new_game_story: extract it again with this dwx");
    const dwe::game::field::TriggerState newGame =
        dwe::game::field::newGameTriggers(**story, warps->startMap, warps->startExit);
    const auto countVia = [&scriptWarps](dwe::game::field::WarpVia via) {
        return static_cast<std::size_t>(std::ranges::count(*scriptWarps, via, &dwe::game::field::ScriptWarp::via));
    };
    CHECK(countVia(dwe::game::field::WarpVia::Talk) == ExpectedTalkWarps);
    CHECK(countVia(dwe::game::field::WarpVia::Other) == ExpectedOtherWarps);
    CHECK(static_cast<std::size_t>(std::ranges::count_if(*scriptWarps, [](const dwe::game::field::ScriptWarp& warp) {
              return warp.transport.has_value();
          })) == ExpectedTransportPicks);

    // The cells of each map with data, and the edges of its warp table that a cell fires.
    std::map<std::string, std::set<std::int32_t>> cells;
    Graph tableWarps;
    for (const dwe::assets::MapEntry& entry : table->maps) {
        const auto map = dwe::assets::loadMapDocument(*vfs, entry.id);
        if (!map) {
            continue;
        }
        std::set<std::int32_t>& values = cells[entry.id];
        for (std::int32_t row = 0; row < dwe::assets::MapCollision::Size; ++row) {
            for (std::int32_t column = 0; column < dwe::assets::MapCollision::Size; ++column) {
                values.insert(map->collision.trigger({.column = column, .row = row}).value_or(0));
            }
        }
        for (const dwe::assets::MapWarp& warp : map->warps) {
            if (warp.targetMapId && values.contains(dwe::core::collision::FirstWarpTrigger + warp.exit)) {
                tableWarps[entry.id].insert(*warp.targetMapId);
            }
        }
    }
    // The scripts' warps the field runs: on a cell the map has, to a map with data.
    Graph newGameWarps = tableWarps;
    Graph anyWarps = tableWarps;
    std::vector<const dwe::game::field::ScriptWarp*> withRecursion;
    std::vector<const dwe::game::field::ScriptWarp*> allowed;
    std::vector<const dwe::game::field::ScriptWarp*> unstructured;
    std::vector<const dwe::game::field::ScriptWarp*> forgetDependent;
    const std::string gameCode = dwe::core::formatMessage(dwe::game::field::messages::GameCodeSite, "");
    std::set<std::tuple<std::string, dwe::game::field::WarpVia, std::int32_t>> cellsDecided;
    for (const dwe::game::field::ScriptWarp& warp : *scriptWarps) {
        if (hasRecursion(warp)) {
            withRecursion.push_back(&warp);
        }
        const bool runs = warp.via == dwe::game::field::WarpVia::Step || warp.via == dwe::game::field::WarpVia::Confirm;
        const auto map = cells.find(warp.map);
        if (!runs || map == cells.end() || !map->second.contains(warp.section) || !warp.targetMap ||
            !cells.contains(*warp.targetMap)) {
            continue;
        }
        anyWarps[warp.map].insert(*warp.targetMap);
        const auto decision = dwe::game::field::decide(warp, newGame, dwe::game::field::VanillaUnknownPolicy);
        if (decision.truth == dwe::game::field::Truth::Unknown &&
            (warp.guard.unstructured || warp.scriptGuard.unstructured)) {
            unstructured.push_back(&warp);
        }
        // A new game takes on each cell only the first warp that is taken, as the field does.
        const auto key = std::tuple{warp.map, warp.via, warp.section};
        if (!cellsDecided.insert(key).second) {
            continue;
        }
        const auto cell = dwe::game::field::warpOnCell(*scriptWarps, warp.map, warp.via, warp.section, newGame,
                                                       dwe::game::field::VanillaUnknownPolicy);
        if (cell.warp != nullptr && cell.warp->targetMap && cells.contains(*cell.warp->targetMap)) {
            newGameWarps[warp.map].insert(*cell.warp->targetMap);
            if (!cell.decision.allowed.empty()) {
                allowed.push_back(cell.warp);
            }
            if (std::ranges::any_of(cell.decision.allowed,
                                    [&](const std::string& reason) { return reason.contains(gameCode); })) {
                forgetDependent.push_back(cell.warp);
            }
        }
    }
    // The cells and sections the policy cannot tell apart, every one the scripts have.
    std::set<std::tuple<std::string, dwe::game::field::WarpVia, std::int32_t>> sources;
    std::vector<std::vector<const dwe::game::field::ScriptWarp*>> ambiguous;
    for (const dwe::game::field::ScriptWarp& warp : *scriptWarps) {
        if (!sources.insert(std::tuple{warp.map, warp.via, warp.section}).second) {
            continue;
        }
        auto cell = dwe::game::field::warpOnCell(*scriptWarps, warp.map, warp.via, warp.section, newGame,
                                                 dwe::game::field::VanillaUnknownPolicy);
        if (!cell.ambiguous.empty()) {
            ambiguous.push_back(std::move(cell.ambiguous));
        }
    }

    const Reach vanilla = reachFrom(newGameWarps, warps->startMap);
    const Reach open = reachFrom(anyWarps, warps->startMap);
    MESSAGE("maps with data ", cells.size(), "; a new game reaches ", vanilla.reached.size(), " (",
            vanilla.withWayBack.size(), " with a way back); every script warp reaches ", open.reached.size(), " (",
            open.withWayBack.size(), " with a way back)");
    CHECK(vanilla.reached == vanilla.withWayBack);
    CHECK(vanilla.withWayBack == std::set<std::string>(ExpectedNewGameMaps.begin(), ExpectedNewGameMaps.end()));
    CHECK(vanilla.reached.contains("room08"));
    CHECK(open.reached.size() == ExpectedOpenReached);
    CHECK(open.withWayBack.size() == ExpectedOpenWithWayBack);

    // Free exploration, through the field's own choice of places: every script warp's map, and the same way back.
    Graph exploreWarps = tableWarps;
    const dwe::game::field::MapFilter hasMap = [&](std::string_view map) { return cells.contains(std::string{map}); };
    std::set<std::tuple<std::string, dwe::game::field::WarpVia, std::int32_t>> cellsExplored;
    for (const dwe::game::field::ScriptWarp& warp : *scriptWarps) {
        const bool runs = warp.via == dwe::game::field::WarpVia::Step || warp.via == dwe::game::field::WarpVia::Confirm;
        const auto map = cells.find(warp.map);
        if (!runs || map == cells.end() || !map->second.contains(warp.section) ||
            !cellsExplored.insert(std::tuple{warp.map, warp.via, warp.section}).second) {
            continue;
        }
        for (const dwe::game::field::ScriptWarp* place :
             dwe::game::field::explorationWarps(*scriptWarps, warp.map, warp.via, warp.section, hasMap)) {
            exploreWarps[warp.map].insert(*place->targetMap);
        }
    }
    const Reach explored = reachFrom(exploreWarps, warps->startMap);
    CHECK(explored.reached == open.reached);
    CHECK(explored.withWayBack == open.withWayBack);
    CHECK(explored.withWayBack.size() == ExpectedOpenWithWayBack);

    // The triggers the warps a new game takes set on their way (sets_flags), written until nothing changes: they
    // open no other map.
    std::map<std::int32_t, bool> written;
    Reach withWrites = vanilla;
    for (bool changed = true; changed;) {
        changed = false;
        const dwe::game::field::TriggerState state = [&](std::int32_t trigger) -> std::optional<bool> {
            const auto found = written.find(trigger);
            return found != written.end() ? std::optional{found->second} : newGame(trigger);
        };
        Graph storyWarps = tableWarps;
        for (const auto& [map, via, section] : cellsExplored) {
            if (!withWrites.reached.contains(map)) {
                continue;
            }
            const auto cell = dwe::game::field::warpOnCell(*scriptWarps, map, via, section, state,
                                                           dwe::game::field::VanillaUnknownPolicy);
            if (cell.warp == nullptr || !cell.warp->targetMap || !cells.contains(*cell.warp->targetMap)) {
                continue;
            }
            storyWarps[map].insert(*cell.warp->targetMap);
            for (const dwe::game::field::FlagEffect& effect : cell.warp->setsFlags) {
                const auto [entry, added] = written.try_emplace(effect.trigger, effect.set);
                changed = changed || added;
                CHECK_MESSAGE(entry->second == effect.set, "trigger ", effect.trigger, " both set and cleared");
            }
        }
        withWrites = reachFrom(storyWarps, warps->startMap);
    }
    std::string writtenText;
    for (const auto& [trigger, set] : written) {
        writtenText += std::format(" {}{}", set ? "+" : "-", trigger);
    }
    MESSAGE("the warps a new game takes write:", writtenText);
    CHECK(withWrites.reached == vanilla.reached);
    CHECK(withWrites.withWayBack == vanilla.withWayBack);
    // None: the opening's 54, 55 and 75 are set by the opening's warp, which the field's start skips.
    CHECK(written.empty());

    // What the vanilla policy decides: the warps whose path goes through a recursion the extraction cut, the ones of
    // the maps a new game reaches that it takes only because the policy allows what is not known, and the ones there
    // that no policy takes because their path is unstructured (exits the field's caution hides; parts (b) and S2).
    CHECK_MESSAGE(withRecursion.size() == ExpectedWarpsWithRecursion,
                  "script warps with a recursion term: ", withRecursion.size(), sourcesOf(withRecursion));
    const auto inVanilla = [&vanilla](const std::vector<const dwe::game::field::ScriptWarp*>& found) {
        std::vector<const dwe::game::field::ScriptWarp*> kept;
        std::ranges::copy_if(found, std::back_inserter(kept),
                             [&vanilla](const auto* warp) { return vanilla.reached.contains(warp->map); });
        return kept;
    };
    const auto allowedInVanilla = inVanilla(allowed);
    const auto unstructuredInVanilla = inVanilla(unstructured);
    CHECK_MESSAGE(allowedInVanilla.size() == ExpectedAllowedInNewGame,
                  "warps of the maps a new game reaches taken only by the unknown policy: ", allowedInVanilla.size(),
                  sourcesOf(allowedInVanilla));
    CHECK_MESSAGE(unstructuredInVanilla.size() == ExpectedUnstructuredInNewGame,
                  "warps of the maps a new game reaches not taken because their path is unstructured: ",
                  unstructuredInVanilla.size(), sourcesOf(unstructuredInVanilla));
    const auto forgetInVanilla = inVanilla(forgetDependent);
    MESSAGE("warps of the maps a new game reaches taken only because game code may have written a trigger: ",
            forgetInVanilla.size(), sourcesOf(forgetInVanilla));
    CHECK_MESSAGE(forgetInVanilla.size() == ExpectedForgetDependentInNewGame,
                  "forget-dependent warps of the maps a new game reaches: ", forgetInVanilla.size());
    std::string ambiguousList;
    std::size_t ambiguousInVanilla = 0;
    for (const auto& warpsOfSource : ambiguous) {
        ambiguousInVanilla += vanilla.reached.contains(warpsOfSource.front()->map) ? 1 : 0;
        ambiguousList += sourcesOf(warpsOfSource);
    }
    MESSAGE("sources whose warps the policy takes to more than one place: ", ambiguous.size(), " (", ambiguousInVanilla,
            " on the maps a new game reaches)", ambiguousList);
    CHECK_MESSAGE(ambiguous.size() == ExpectedAmbiguousSources, "ambiguous sources: ", ambiguous.size());
    CHECK_MESSAGE(ambiguousInVanilla == ExpectedAmbiguousInNewGame,
                  "ambiguous sources on the maps a new game reaches: ", ambiguousInVanilla);

    for (const auto& [map, mapCells] : cells) {
        static_cast<void>(mapCells);
        std::optional<Exception> found;
        if (!open.reached.contains(map)) {
            found = Exception::Unreached;
        } else if (!open.withWayBack.contains(map)) {
            found = Exception::WithoutWayBack;
        }
        const auto expected = std::ranges::find(ExpectedExceptions, map, &MapException::map);
        if (expected == ExpectedExceptions.end()) {
            CHECK_MESSAGE(!found.has_value(), "a map every script warp leaves out, and not in the table: ", map);
            continue;
        }
        INFO(map, ": ", expected->reason);
        CHECK(found == expected->kind);
    }
}

TEST_CASE("the user's disc, extracted: a start after the opening leaves the town's cells to their doors") {
    // The town's cell under the new game's spawn runs the opening while trigger 55 is clear and is the door of room08
    // after it; the other cell runs the opening only while 75 and 54 are clear (script 164, sections 54 and 66).
    constexpr std::int32_t SpawnCell = 54;
    constexpr std::int32_t OpeningOnlyCell = 66;
    const dwe::assets::DiskFileSystem disk;
    const auto vfs = mountContent(disk);
    if (!vfs) {
        MESSAGE("DWE_CONTENT is not set: skipping the opening of extracted content");
        return;
    }
    const auto warps = dwe::game::field::loadFieldWarps(*vfs);
    const auto scriptWarps = dwe::game::field::loadScriptWarps(*vfs);
    const auto story = dwe::assets::loadNewGameStory(*vfs);
    REQUIRE_MESSAGE(warps.has_value(), errorOf(warps));
    REQUIRE_MESSAGE(scriptWarps.has_value(), errorOf(scriptWarps));
    REQUIRE_MESSAGE(story.has_value(), errorOf(story));
    const std::optional<dwe::assets::NewGameStory> found = story.value_or(std::nullopt);
    if (!found || !found->opening) {
        FAIL("the content has no data/new_game_story with an opening: extract it again with this dwx");
        return;
    }
    const dwe::assets::NewGameStory& newGame = *found;
    const dwe::assets::NewGameOpening& opening = *found->opening;
    CHECK(newGame.map == "twna01");
    CHECK(opening.exit == 9);
    CHECK(opening.section == SpawnCell);
    CHECK(opening.toMap == std::optional<std::string>{"topn01"});
    CHECK(opening.toExit == 9);
    CHECK(opening.sets == std::vector<std::int32_t>{54, 55, 75});
    REQUIRE(warps->startMap == newGame.map);
    REQUIRE(warps->startExit != opening.exit);

    const auto cellGoesTo = [&](const dwe::game::field::TriggerState& state, std::int32_t section) {
        const auto cell = dwe::game::field::warpOnCell(*scriptWarps, newGame.map, dwe::game::field::WarpVia::Step,
                                                       section, state, dwe::game::field::VanillaUnknownPolicy);
        return cell.warp != nullptr ? cell.warp->targetMap : std::nullopt;
    };
    // Before the opening, as on the spawn, both cells run it.
    const auto ahead = dwe::game::field::newGameTriggers(newGame, newGame.map, opening.exit);
    CHECK(cellGoesTo(ahead, SpawnCell) == opening.toMap);
    CHECK(cellGoesTo(ahead, OpeningOnlyCell) == opening.toMap);
    // From the field's start, which skips it, the spawn's cell is room08's door and the other does nothing.
    const auto skipped = dwe::game::field::newGameTriggers(newGame, warps->startMap, warps->startExit);
    CHECK(cellGoesTo(skipped, SpawnCell) == std::optional<std::string>{"room08"});
    CHECK_FALSE(cellGoesTo(skipped, OpeningOnlyCell).has_value());
}
