#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/tournament/bracket_animation.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// The parts of tournament.yaml around the cups' matches: how the bracket shows, the prizes a
// ruleset adds to a cup won and the tournaments' medal (main/tournament.c:
// checkTournamentMedalConditions). Each is optional: a ruleset without one has none.
namespace dwe::game::tournament {

// tournament.yaml's `bracket` (std/std_setup.c: STD_tournamentMain; std/std_main.c:
// STD_addBracketIntro, STD_addBracket, STD_drawBracket, STD_tickBracketHitFlash).
struct BracketRules {
  // The board's frames a second (VSync(3): 20).
  std::int32_t framesPerSecond = 0;
  // The bank loaded in slot 8 for the board's sounds (loadMapSounds(0x10)).
  std::int32_t soundBank = 0;
  // playMusic(font, track): the intro's, a match's board and the champion's climb.
  std::int32_t musicFont = 0;
  std::int32_t introTrack = 0;
  std::int32_t matchTrack = 0;
  std::int32_t championTrack = 0;
  // After the champion's climb: fadeToBlack(10) and 11 frames.
  std::int32_t championFadeFrames = 0;
  std::int32_t championTailFrames = 0;
  // The pictures: the board, the two sprite pages and the folder of the grades' cards.
  std::string board;
  std::array<std::string, 2> pages;
  std::string cards;
  // The hit flash's cell (STD_tickBracketHitFlash: 16 x 16 at (0, 200) of the second page).
  BracketCell flash;
};
core::Result<std::optional<BracketRules>> parseBracketRules(const ruleset::Section& section);

// The picture of a cup's grade card (STD_GRADES[cup]: data/stadium's `grades`, disc paths such as
// \STDDAT\GRADE\GRADED.TIM): `<cards>/graded`; empty for a cup without one.
[[nodiscard]] std::string bracketCardImage(const BracketRules& rules,
                                           std::span<const std::string> grades, std::int32_t cup);
// data/stadium's `grades`; empty in an extraction without them.
[[nodiscard]] std::vector<std::string> parseStadiumGrades(const core::YamlNode& stadium);

// tournament.yaml's `extra_prizes`: what a ruleset gives besides the arena script's prizes when a
// cup is won (an engine hook; the original's are all in the script, so its list is empty).
struct ExtraPrize {
  std::int32_t cup = 0;
  // The item by the original's number and how many; money in bits.
  std::optional<std::int32_t> item;
  std::int32_t amount = 1;
  std::int32_t money = 0;
};
core::Result<std::vector<ExtraPrize>> parseExtraPrizes(const ruleset::Section& section);
// The prizes of one cup, in the list's order.
[[nodiscard]] std::vector<ExtraPrize> extraPrizesOf(std::span<const ExtraPrize> prizes,
                                                    std::int32_t cup);

// tournament.yaml's `medal` (checkTournamentMedalConditions, scriptCheckTournamentMedal).
struct TournamentMedalRules {
  // The groups of cups, each its first won trigger and how many: all won earns the group's medal.
  std::vector<std::pair<std::int32_t, std::int32_t>> cupGroups;
  // TOURNAMENT_WINS from this many on, the next medal.
  std::int32_t wins = 0;
  // Every card below `cards` held and the game beaten once (its trigger), the last one.
  std::int32_t cards = 0;
  std::int32_t beatenTrigger = 0;
  // showMapHeadTextbox(result, 0xff, 0, section): the line is the medal's result.
  std::int32_t linesSection = 0;
};
core::Result<std::optional<TournamentMedalRules>>
parseTournamentMedalRules(const ruleset::Section& section);

// What checkTournamentMedalConditions reads.
struct TournamentMedalFacts {
  std::function<bool(std::int32_t)> trigger;
  std::int32_t tournamentWins = 0;
  std::function<std::int32_t(std::int32_t)> cardCopies;
  std::function<bool(std::int32_t)> hasMedal;
};
// The result (0 to cupGroups.size() + 1), or none. Like the original only the first condition
// that holds counts: when its medal is held already there is none, even if a later one holds.
// `medalIds` is data/tournaments' medal_ids (TOURNAMENT_MEDAL_IDS) by result.
[[nodiscard]] std::optional<std::int32_t> tournamentMedal(const TournamentMedalRules& rules,
                                                          std::span<const std::int32_t> medalIds,
                                                          const TournamentMedalFacts& facts);

} // namespace dwe::game::tournament
