#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/formats/script/guarded_walk.hpp>
#include <dwe/formats/script/map_changes.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// The map changes the event scripts make on each map, and the triggers a new game starts with.
// Entering a map runs the section of MAPHEAD (script 0) with the map's id, which loads the map's
// script with SET_SCRIPT (map.c: changeMap, script_interp.c: runMapHeadScript); that script's
// sections run on stepping on a trigger cell, on pressing confirm on one, on talking to an NPC and
// on entering the map (tamer.c: checkMapInteraction).
namespace dwe::formats::script {

// Script 0, the one initializeScripts loads from SCN/MAPHEAD.SCN.
inline constexpr std::uint16_t MapHeadScript = 0;
// The section of MAPHEAD a new game runs (main.c: newGameScene): the one of the first map.
inline constexpr std::uint16_t NewGameSection = 0xCC;
// The section of a map's script that runs when the map is entered.
inline constexpr std::uint16_t EnterSection = 0xFE;

// What runs a section of a map's script (tamer.c: checkMapInteraction): stepping on a cell whose
// collision value is the section (51-79), pressing confirm on one (80-109), entering the map
// (EnterSection, and the map's own MAPHEAD section), talking to an NPC of the map whose script id
// it is (Talk), or anything else: another section, or an NPC the extraction was not told of.
enum class WarpVia : std::uint8_t { Step, Confirm, Enter, Other, Talk };

WarpVia warpViaOf(std::uint16_t section);

// The transport menu (shop/script_menu.c: tickBirdraTransport): call_routine TransportRoutine opens
// it, it lists the entries of the executable's table whose trigger is set (birdraFillTargetList),
// and a pick of one the money pays for writes its map and exit to pstats 247 and 248 and hands the
// script over to MAPHEAD's TransportSection, which teleports there; a cancel goes back to the
// calling script (ACTIVE_INSTRUCTION = 0).
inline constexpr std::uint8_t TransportRoutine = 0x0A;
inline constexpr std::uint16_t TransportSection = 0x4E3;

// An entry of the transport table (script.h: BIRDRA_TRANSPORT_TARGETS), as the executable has it.
struct TransportDestination {
  std::uint16_t map = 0;
  std::uint8_t exit = 0;
  std::uint16_t trigger = 0; // listed while it is set
  std::uint32_t cost = 0;    // picked only when MONEY reaches it

  bool operator==(const TransportDestination&) const = default;
};

// The transport menu's pick that makes a map change: the table's entry and its cost.
struct TransportPick {
  std::uint8_t entry = 0;
  std::uint32_t cost = 0;

  bool operator==(const TransportPick&) const = default;
};

// A map change a section run on a map makes.
struct ScriptWarp {
  std::uint16_t map = 0; // the map's id: the MAPHEAD section that loads the script
  WarpVia via = WarpVia::Other;
  std::uint16_t section = 0;
  std::uint16_t script = 0; // the map's script, or MapHeadScript for the map's MAPHEAD section
  // What MAPHEAD takes to load that script on entering the map; empty for the MAPHEAD section
  // itself.
  PathGuard scriptGuard;
  // For a transport pick, the path to the call_routine, with the entry's trigger, its cost and the
  // pick as the last terms, and the entry's map and exit.
  MapChange change;
  std::optional<TransportPick> transport = std::nullopt;

  bool operator==(const ScriptWarp&) const = default;
};

struct ScriptWarps {
  std::vector<ScriptWarp> warps; // by map, then by section, then in the order the walk finds them
  // The MAPHEAD sections that load no script: the ones the game runs for other things than entering
  // a map.
  std::vector<std::uint16_t> otherMapHeadSections;
};

// Whether an NPC of a map (by its id) has a section of the map's script as its script id.
using SectionTalks = std::function<bool(std::uint16_t map, std::uint16_t section)>;

// What findScriptWarps knows besides the scripts: which sections NPCs run, and the transport table.
struct ScriptWarpSources {
  SectionTalks talks;
  std::vector<TransportDestination> transport;
};

// Every map change of every section of every map's script, and of the MAPHEAD sections that load a
// script, with the transport menu's picks of every call that opens it. The lookup must know MAPHEAD
// as MapHeadScript.
ScriptWarps findScriptWarps(const ScriptLookup& lookup, const ScriptWarpSources& sources = {});

// The exit of the first map a new game puts the player at (map.c: initializeMap sets CURRENT_EXIT).
inline constexpr std::int32_t NewGameExit = 9;

// The map change a new game makes before the player can move: step cells run their section on every
// tick the player stands on them (tamer.c: checkMapInteraction), so a spawn on one runs it at once.
// On the disc it is the opening of the story, which sets the triggers that keep the town's cells
// from running it again.
struct NewGameOpening {
  std::int32_t exit = NewGameExit;
  std::uint16_t section = 0; // the step section of the cell under the spawn
  std::uint16_t screen = 0;  // where it goes
  std::int32_t screenExit = 0;
  std::vector<std::uint16_t> sets; // sorted: the triggers its path leaves set

  bool operator==(const NewGameOpening&) const = default;
};

// The triggers set when a new game gives the player control: those that MAPHEAD's NewGameSection
// and the EnterSection of the script it loads set, from all triggers clear (initializeScripts), on
// every path. Nothing if MAPHEAD has no such section.
struct NewGameTriggers {
  std::uint16_t map = 0;
  std::uint16_t script = 0;
  std::vector<std::uint16_t> set; // sorted
  // Whether every path ends with the same triggers set; if not, `set` holds the ones they share.
  bool agreed = true;
  // Sorted: the triggers the game code may have set before the new-game section runs, which the
  // walk does not model (StartingPartnerRaisedTriggers). They are not known either way.
  std::vector<std::uint16_t> unknown;
  // The map change the cell under the spawn makes (findNewGameOpening), if it makes one for
  // certain.
  std::optional<NewGameOpening> opening;

  bool operator==(const NewGameTriggers&) const = default;
};

std::optional<NewGameTriggers> findNewGameTriggers(const ScriptLookup& lookup);

// How many triggers of NewGameTriggers::unknown findNewGameOpening tries each way (2^n walks); with
// more it finds no opening.
inline constexpr std::size_t MaximumOpeningUnknowns = 8;

// The map change of the section that a spawn on a cell of value `spawnCell` runs, in the state the
// new game leaves
// (`set`, every other trigger clear): a failure that says why unless the cell is a step cell
// (51-79), every path of the new game agrees (`agreed`) and the section makes exactly one map
// change to a screen, through IFs that state decides, the same whichever of the `unknown` triggers
// are set, and without clearing a trigger of `set`. `sets` holds the triggers set at the change.
core::Result<NewGameOpening> findNewGameOpening(const NewGameTriggers& newGame,
                                                std::uint8_t spawnCell, const ScriptLookup& lookup);

// The raised triggers of the two partners a new game may start with (main.c: main raises one of
// them by pstat 0xfe before it runs the new-game section; script_common.c: setDigimonRaised sets
// trigger 0x200 + its id).
inline constexpr std::array<std::uint16_t, 2> StartingPartnerRaisedTriggers = {0x203, 0x211};

// The id of a map in data/maps, by its number; nothing for a number no map has.
using MapName = std::function<std::optional<std::string>(std::uint16_t map)>;

// field/script_warps (content/schemas/script_warps.schema.json): one entry per warp with the map,
// what runs it
// (`via`), the section, where it goes (`to`: a map and exit, or `screen` for a number no map has;
// `teleport`: the variable that holds it), the guards (`script_guard`, `guard`: IF terms with the
// listing's conditions and the result they must give, choice terms with the arm), the triggers it
// sets on the way (`sets_flags`), the transport menu's entry and cost for a pick of it
// (`transport`) and where it is (`source`). The lookup gives the offsets of the sources.
core::YamlNode scriptWarpsToYaml(const ScriptWarps& warps, const ScriptLookup& lookup,
                                 const MapName& mapName);

// data/new_game_story (content/schemas/new_game_story.schema.json): the map, its script, the
// triggers set and the opening, when there is one.
core::YamlNode newGameTriggersToYaml(const NewGameTriggers& triggers, const MapName& mapName);

} // namespace dwe::formats::script
