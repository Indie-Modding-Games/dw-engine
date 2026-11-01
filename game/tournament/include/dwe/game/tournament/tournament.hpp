#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/ruleset/random.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The arena's tournaments (docs/modulos/game/tournament.md): the rivals a cup draws, the bracket
// of eight with the matches the player does not play rolled by level, the rivals' stats and
// moves, and how many wins take the cup (main/utils2.c: startTournament, std/std_setup.c:
// STD_tournamentMain and STD_initializeOpponent, std/std_main.c: STD_setupParticipants). Species
// are the original's numbers (DIGIMON_DATA index); the session turns them into rows.
namespace dwe::game::tournament {

inline constexpr std::string_view TournamentSectionName = "tournament";

// The fighters of a cup besides the player's partner, and the matches the partner plays.
inline constexpr std::size_t Rivals = 7;
inline constexpr std::size_t Entrants = 8;
inline constexpr std::size_t Rounds = 3;

// The stat scale of a rival's level (STD_initializeOpponent: x1, x2, x2.5).
struct LevelScale {
  std::int32_t level = 0;
  std::int32_t multiply = 1;
  std::int32_t divide = 1;
};

struct TournamentRules {
  // The story values a tournament reads and writes: the cup entered (pstat 3), the wins it ends
  // with (pstat 255), the registration (trigger 37), a cup won (trigger 15 + cup) and the
  // residents of the city (trigger 200 + species).
  std::int32_t cupVariable = 0;
  std::int32_t resultVariable = 0;
  std::int32_t registeredTrigger = 0;
  std::int32_t wonTriggerBase = 0;
  std::int32_t recruitTriggerBase = 0;
  // A cup without a rival list draws from every resident of the species below this number.
  std::int32_t speciesCount = 0;
  // The cup whose rival list is taken whole, residents or not.
  std::int32_t wholeListCup = 0;
  // The wins that take a cup, and the cups that take another number (startTournament: cup 5, 2).
  std::int32_t winsNeeded = 0;
  std::vector<std::pair<std::int32_t, std::int32_t>> cupWinsNeeded;
  // Species the arena has no fighter for, and the one that stands in (STD_tournamentMain).
  std::vector<std::pair<std::int32_t, std::int32_t>> standIns;
  // The match clock in seconds (STD_addFighterCounter) and the share of its maximum HP and MP the
  // partner gets back after each match (STD_deinitializeCombat: a fifth).
  std::int32_t timeLimit = 0;
  std::int32_t recoveryDivisor = 1;
  // A rival's stats: base +- spread percent, HP and MP `vitalityFactor` times that, scaled by
  // level, at most `statCap` and `vitalityCap`.
  std::int32_t statSpread = 0;
  std::int32_t vitalityFactor = 1;
  std::int32_t statCap = 0;
  std::int32_t vitalityCap = 0;
  std::vector<LevelScale> levels;
  // The level of the win chances' first row and column (STD_WIN_CHANCES[level - 3]).
  std::int32_t firstLevel = 0;
  // A rival's moves: of the species' first `moveSlots`, the techniques (not the finishers, moves
  // `finishers[0]` to `finishers[1] - 1`) whose MP cost times `mpCostFactor` its MP over
  // `mpDivisor` pays.
  std::int32_t moveSlots = 0;
  std::array<std::int32_t, 2> finishers{};
  std::int32_t mpCostFactor = 1;
  std::int32_t mpDivisor = 1;
  // How much a rival likes each equipped slot (STD_tickEnemyAI: 30, 30, 30, 0).
  std::array<std::int32_t, 4> movePriorities{};

  // data/stadium (the executable's STD tables): the rivals' base stat by cup and the chance in
  // percent that a fighter of a level beats one of another (row: the first's level).
  std::vector<std::int32_t> opponentBaseStats;
  std::vector<std::vector<std::int32_t>> winChances;
};

// The ruleset's section and data/stadium's document.
core::Result<TournamentRules> parseTournamentRules(const ruleset::Section& section,
                                                   const core::YamlNode& stadium);

// One cup's lists in the arena's map script (tournament.c: getCupDataJumpTable, section 10): the
// entry requirements, the species too strong for it, the species it admits and its rivals. A list
// that opens with the end mark (0xFE) is empty: for the rivals, every resident.
struct CupData {
  std::vector<std::int32_t> requirements;
  std::vector<std::int32_t> overlevel;
  std::vector<std::int32_t> eligible;
  std::vector<std::int32_t> rivals;
  // Where the cup's text is in the script (entry 0: the show_textbox initTournamentInfo shows).
  std::optional<std::uint32_t> textAt;
};

// A list's species as the script stores them: bytes up to the first 0xFE or 0xFF.
std::vector<std::int32_t> cupList(std::span<const std::uint8_t> bytes);

// The section of a map script that holds the cups' lists (startTournament:
// getCupDataJumpTable(10)).
inline constexpr std::int32_t CupDataSection = 10;

// tournament.c: getCupDataJumpTable: a cup's lists in a script listing as dwx writes it (sections,
// instructions with `at`, `op`, `target` and a data block's `data` bytes): the section's jump table
// leads to the cup's, whose entries 1 to 4 lead to its lists. Fails when the script has no such
// cup.
core::Result<CupData> cupDataOf(const core::YamlNode& listing, std::int32_t cup);

// A section of a map script that is a list of species, as cupList reads it (dget.c:
// isTournamentEnabled reads the active map's section 11). Empty when the script lacks it.
std::vector<std::int32_t> sectionList(const core::YamlNode& listing, std::int32_t section);

// startTournament: the seven rivals. `resident(species)` tells whether the species lives in the
// city. Fails when the cup has nobody to draw.
core::Result<std::array<std::int32_t, Rivals>>
drawRivals(const TournamentRules& rules, std::int32_t cup, const CupData& data,
           const std::function<bool(std::int32_t)>& resident, std::int32_t partnerSpecies,
           ruleset::Random& random);

// The bracket (STD_setupParticipants): the partner in a random slot, the rivals shuffled into the
// others, the matches it does not play rolled by level, and the three it plays.
struct Bracket {
  std::size_t playerSlot = 0;
  std::array<std::int32_t, Entrants> entrants{};
  // The slots that won the first round and the second round's half the partner is not in.
  std::array<std::size_t, Entrants / 2> firstRound{};
  std::array<std::size_t, 2> secondRound{};
  // The species the partner meets in each round.
  std::array<std::int32_t, Rounds> opponents{};
};

// `rivals` as drawRivals gave them; the stand-ins go in first (STD_tournamentMain).
// `levelOf(species)` is the species' level (DIGIMON_DATA level).
Bracket drawBracket(const TournamentRules& rules, std::array<std::int32_t, Rivals> rivals,
                    std::int32_t partnerSpecies,
                    const std::function<std::int32_t(std::int32_t)>& levelOf,
                    ruleset::Random& random);

// What STD_initializeOpponent reads of a species' move slot.
struct SlotMove {
  std::int32_t number = 0;
  std::int32_t mpCost = 0;
  std::int32_t range = 0;
};

// What it reads of the species: its level and its sixteen move slots (none: 0xFF).
struct RivalSpecies {
  std::int32_t level = 0;
  std::array<std::optional<SlotMove>, 16> moves{};
};

// A rival as it enters the match: its stats and its four equipped slots (the species' move slot
// each holds; the fourth is the finisher).
struct Rival {
  std::int32_t offense = 0;
  std::int32_t defense = 0;
  std::int32_t speed = 0;
  std::int32_t brain = 0;
  std::int32_t hp = 0;
  std::int32_t mp = 0;
  std::array<std::optional<std::size_t>, 4> slots{};
};

// STD_initializeOpponent, with the cup's base stat.
Rival makeRival(const TournamentRules& rules, std::int32_t cup, const RivalSpecies& species,
                ruleset::Random& random);

// startTournament: the wins that take `cup`.
std::int32_t winsNeeded(const TournamentRules& rules, std::int32_t cup);

// The species the arena puts in for one it has no fighter for (STD_tournamentMain).
std::int32_t standIn(const TournamentRules& rules, std::int32_t species);

// The registration at the arena's counter (story routine 18, dget/dget.c: initTournamentSchedule).

// The schedule shows five days of up to six cups each (fillEnabledTournamentTable).
inline constexpr std::size_t ScheduleDays = 5;
inline constexpr std::size_t ScheduleRows = 6;

struct ScheduleRules {
  // The values the registration writes: the day entered (pstat 2: the day, or the next one plus
  // `tomorrowFlag`), the partner's species (pstat 4), the registration's other triggers (38 and 39
  // besides the registration's own) and the trigger of an overlevel entry (40).
  std::int32_t dayVariable = 0;
  std::int32_t speciesVariable = 0;
  std::int32_t tomorrowFlag = 0;
  std::vector<std::int32_t> registeredTriggers;
  std::int32_t overlevelTrigger = 0;
  // Today's cups take entries up to this minute of the day (minutesOfDay() <= 600).
  std::int32_t lastMinuteToday = 0;
  // The days of the schedule's cycle (day % 30).
  std::int32_t cycleDays = 0;
  // isTournamentEnabled: the residents' triggers counted when a cup lists no rivals (200 to 311),
  // the trigger of requirement type 1 (the Grade A cup won) and the active map's section with the
  // species of requirement type 2 (11).
  std::int32_t recruitTriggerEnd = 0;
  std::int32_t gradeAWonTrigger = 0;
  std::int32_t localListSection = 0;
  // The MAPHEAD lines the registration says (showMapHeadTextbox(n, ..., 0x4d8)): the schedule's
  // title, the question, the question for an overlevel partner and the choice.
  std::int32_t linesSection = 0;
  std::int32_t titleLine = 0;
  std::int32_t askLine = 0;
  std::int32_t askOverlevelLine = 0;
  std::int32_t choiceLine = 0;
  // tickTournamentSchedule's sound for a cell that cannot be picked (playSound(0, 0xB)); the
  // cursor's, the pick's and the close's are the menus' own (2, 3 and 4).
  std::int32_t soundSlot = 0;
  std::int32_t refuseSound = 0;
};

// The section's schedule part.
core::Result<ScheduleRules> parseScheduleRules(const ruleset::Section& section);

// data/tournaments: each cup's grade mark (TOURNAMENT_GRADES) and the cups of each day of the
// cycle (TOURNAMENT_SCHEDULE), by cup number.
struct TournamentTable {
  std::vector<std::string> grades;
  std::vector<std::vector<std::int32_t>> days;
  // The tournaments' medals by checkTournamentMedalConditions' result (TOURNAMENT_MEDAL_IDS).
  std::vector<std::int32_t> medalIds;
};
core::Result<TournamentTable> parseTournamentTable(const core::YamlNode& document);

// What isTournamentEnabled reads besides the cup's lists.
struct CupOpening {
  // Whether a story trigger is set.
  std::function<bool(std::int32_t)> trigger;
  // The active map's list of requirement type 2.
  std::span<const std::int32_t> localList;
};

// dget.c: isTournamentEnabled: whether the cup is on the schedule at all.
bool cupOpen(const TournamentRules& rules, const ScheduleRules& schedule, const CupData& data,
             const CupOpening& opening);

// A cup on the schedule: whether the partner may enter (tournamentCheckEligible: its species in
// the cup's eligible list) and whether it is too strong for it (tournamentCheckFair: in the
// overlevel list). Only today's and tomorrow's cups are checked; the others are never open.
struct ScheduleCell {
  std::int32_t cup = 0;
  bool eligible = false;
  bool overlevel = false;
  // tickTournamentSchedule: a cell with either mark can be entered.
  [[nodiscard]] bool enterable() const { return eligible || overlevel; }
};

struct Schedule {
  // The day of the cycle of the first column (DAY).
  std::int32_t firstDay = 0;
  std::array<std::array<std::optional<ScheduleCell>, ScheduleRows>, ScheduleDays> days{};
};

// fillEnabledTournamentTable: from `day` on, each day's open cups in the table's order; today's are
// checked only up to the last minute to enter.
struct ScheduleFacts {
  std::int32_t day = 0;
  std::int32_t minuteOfDay = 0;
  std::function<bool(std::int32_t)> open;
  std::function<bool(std::int32_t)> eligible;
  std::function<bool(std::int32_t)> overlevel;
};
Schedule buildSchedule(const ScheduleRules& rules, const TournamentTable& table,
                       const ScheduleFacts& facts);

// initTournamentSchedule state 5: the day the entry is for, as pstat 2 keeps it (the column
// picked: 0 today, else tomorrow with the flag).
std::int32_t entryDay(const ScheduleRules& rules, std::int32_t day, std::size_t column);

// The arena the cups are fought in (std_setup.c: STD_initializeTournament, STD_loadArenaAssets;
// std_main.c: STD_placePlayer1/2, STD_runIntro, STD_setCameraOrbit). Points are the original's
// axes and units (y down).
struct ArenaPoint {
  std::int32_t x = 0;
  std::int32_t z = 0;

  friend bool operator==(const ArenaPoint&, const ArenaPoint&) = default;
};

// One arena: its model and collision grid in the packs, where the fighters stand when it opens
// (the partner at +startX, the rival at -startX) and how far each walks in toward the other in its
// intro (STD_runIntro: dist).
struct Arena {
  std::string model;
  std::string collision;
  std::int32_t startX = 0;
  std::int32_t walkIn = 0;
};

// STD_setCameraOrbit: the camera looks across the fighters from the side, `eyeHeight` over the
// ground, back from their midpoint by their distance times `projection` / `distanceDivisor`
// within [minDistance, maxDistance] (minDistance being STD_DEFAULT_CAM_MIN_DISTANCE once the match
// runs), tilted down so the midpoint shows `drop` under the eye.
struct ArenaCamera {
  std::int32_t projection = 0;
  std::int32_t distanceDivisor = 1;
  std::int32_t minDistance = 0;
  std::int32_t maxDistance = 0;
  std::int32_t eyeHeight = 0;
  std::int32_t drop = 0;
};

struct ArenaRules {
  // By the original's arena id (STD_ARENA_MODELS).
  std::vector<Arena> arenas;
  // The map whose cups use arena 1 (STD_tournamentMain: CURRENT_SCREEN 0x6a); the others use 0.
  std::string secondArenaMap;
  // setEntityRotation in STD_placePlayer1 and STD_placePlayer2.
  std::int32_t partnerYaw = 0;
  std::int32_t rivalYaw = 0;
  ArenaCamera camera;
};

core::Result<ArenaRules> parseArenaRules(const ruleset::Section& section);

// STD_tournamentMain: the arena of a cup entered on this map.
[[nodiscard]] std::size_t arenaFor(const ArenaRules& rules, std::string_view map);

// Where the fighters stand once the intro is over (STD_setPostIntroPosition: the start less the
// walk in, toward the arena's centre).
struct ArenaStart {
  ArenaPoint partner;
  ArenaPoint rival;
};
[[nodiscard]] ArenaStart arenaStart(const Arena& arena);

// STD_setCameraOrbit: the eye and the point it looks at, in the original's axes (y down).
struct ArenaView {
  std::array<std::int32_t, 3> eye{};
  std::array<std::int32_t, 3> target{};
};
[[nodiscard]] ArenaView arenaView(const ArenaCamera& camera, ArenaPoint partner, ArenaPoint rival);

} // namespace dwe::game::tournament
