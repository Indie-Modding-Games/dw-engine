#include <dwe/assets/file_system.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/tournament/tournament.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>

namespace {

using namespace dwe::game::tournament;
using dwe::ruleset::Random;

// data/stadium's shape, with the original's numbers for the tables these tests read.
constexpr std::string_view Stadium = R"(opponent_base_stats: [100, 150, 200, 250, 300, 0]
win_chances:
- [50, 20, 5]
- [80, 50, 15]
- [95, 85, 50]
grades: []
)";

TournamentRules originalRules() {
  const auto bytes = dwe::assets::DiskFileSystem{}.readFile(
      std::filesystem::path{DWE_GAME_PACK} / "rules" / "original" / "tournament.yaml");
  REQUIRE(bytes);
  auto node = dwe::core::parseYaml(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  REQUIRE(node);
  auto stadium = dwe::core::parseYaml(Stadium);
  REQUIRE(stadium);
  auto rules = parseTournamentRules(
      {.name = "tournament", .owner = "test", .file = "tournament.yaml", .node = *node}, *stadium);
  REQUIRE_MESSAGE(rules, (rules ? std::string{} : rules.error().message));
  return *rules;
}

bool everyone(std::int32_t /*species*/) {
  return true;
}

} // namespace

TEST_CASE("tournament.yaml: the section reads with the original's numbers") {
  const TournamentRules rules = originalRules();
  CHECK(rules.cupVariable == 3);
  CHECK(rules.timeLimit == 99);
  CHECK(rules.opponentBaseStats.at(4) == 300);
  CHECK(rules.winChances.at(1).at(2) == 15);
  CHECK(winsNeeded(rules, 0) == 3);
  CHECK(winsNeeded(rules, 5) == 2);
  CHECK(standIn(rules, 64) == 54);
  CHECK(standIn(rules, 12) == 12);
}

TEST_CASE("tournament.c getCupDataJumpTable: a cup list ends at 0xFE or 0xFF") {
  const std::array<std::uint8_t, 5> bytes{0x0C, 0x0D, 0xFE, 0x0E, 0xFF};
  CHECK(cupList(bytes) == std::vector<std::int32_t>{0x0C, 0x0D});
  const std::array<std::uint8_t, 2> empty{0xFE, 0xFF};
  CHECK(cupList(empty).empty());
}

TEST_CASE("utils2.c startTournament: the rivals come from the cup's residents") {
  const TournamentRules rules = originalRules();
  Random random{11};
  const auto resident = [](std::int32_t species) { return species % 2 == 0; };

  CupData few;
  few.rivals = {10, 11, 12, 14};
  const auto filled = drawRivals(rules, 0, few, resident, 99, random);
  REQUIRE(filled);
  // The residents in order, then repeats drawn from them.
  CHECK((*filled)[0] == 10);
  CHECK((*filled)[1] == 12);
  CHECK((*filled)[2] == 14);
  CHECK(std::ranges::all_of(*filled, [](std::int32_t s) { return s == 10 || s == 12 || s == 14; }));

  // The whole-list cup takes its list, residents or not.
  const auto whole = drawRivals(rules, rules.wholeListCup, few, resident, 99, random);
  REQUIRE(whole);
  CHECK((*whole)[1] == 11);

  // More than seven: never the partner's species, and seven different ones.
  CupData many;
  many.rivals = {2, 4, 6, 8, 10, 12, 14, 16, 18};
  const auto drawn = drawRivals(rules, 0, many, everyone, 8, random);
  REQUIRE(drawn);
  CHECK(std::ranges::find(*drawn, 8) == drawn->end());
  CHECK(std::set<std::int32_t>(drawn->begin(), drawn->end()).size() == Rivals);

  // No list: every resident species.
  const auto open =
      drawRivals(rules, 0, CupData{}, [](std::int32_t s) { return s < 3; }, 0, random);
  REQUIRE(open);
  CHECK(std::ranges::all_of(*open, [](std::int32_t s) { return s < 3; }));

  CHECK_FALSE(drawRivals(rules, 0, few, [](std::int32_t) { return false; }, 0, random));
}

TEST_CASE("std_main.c STD_setupParticipants: the partner meets its pair, the next pair and the "
          "other half") {
  TournamentRules rules = originalRules();
  // Level 3 always beats level 3: every rolled match goes to the first slot.
  rules.winChances = {{100}};
  const auto level = [](std::int32_t /*species*/) { return 3; };
  for (std::uint64_t seed = 1; seed < 40; ++seed) {
    Random random{seed};
    const Bracket bracket = drawBracket(rules, {64, 21, 22, 23, 24, 25, 26}, 1, level, random);
    CHECK(bracket.entrants.at(bracket.playerSlot) == 1);
    CHECK(std::ranges::find(bracket.entrants, 64) == bracket.entrants.end());
    CHECK(std::ranges::find(bracket.entrants, 54) != bracket.entrants.end());
    const std::size_t slot = bracket.playerSlot;
    CHECK(bracket.opponents[0] == bracket.entrants.at(slot ^ 1U));
    // The other pair of its quarter: its first slot won.
    const std::size_t pair = (slot / 2) ^ 1U;
    CHECK(bracket.opponents[1] == bracket.entrants.at(pair * 2));
    // The other half: its first quarter's first slot won.
    const std::size_t half = slot < 4 ? 4 : 0;
    CHECK(bracket.opponents[2] == bracket.entrants.at(half));
  }
}

TEST_CASE("std_setup.c STD_initializeOpponent: stats by cup and level, moves it can pay") {
  TournamentRules rules = originalRules();
  rules.statSpread = 0;
  RivalSpecies champion{.level = 4, .moves = {}};
  champion.moves[0] = SlotMove{.number = 0, .mpCost = 27, .range = 2};
  champion.moves[1] = SlotMove{.number = 1, .mpCost = 61, .range = 2};
  champion.moves[2] = SlotMove{.number = 60, .mpCost = 10, .range = 2};
  Random random{5};
  const Rival rival = makeRival(rules, 1, champion, random);
  CHECK(rival.offense == 300);
  CHECK(rival.brain == 300);
  CHECK(rival.hp == 3000);
  CHECK(rival.mp == 3000);
  // MP 3000 / 10 = 300 pays 27 * 3 but 61 * 3 = 183 too; the finisher (60) only in the fourth.
  CHECK(rival.slots[3] == std::optional<std::size_t>{2});
  std::set<std::size_t> techniques;
  for (std::size_t place = 0; place < 3; ++place) {
    if (rival.slots.at(place)) {
      techniques.insert(*rival.slots.at(place));
    }
  }
  CHECK(techniques == std::set<std::size_t>{0, 1});

  RivalSpecies ultimate{.level = 5, .moves = {}};
  const Rival strong = makeRival(rules, 4, ultimate, random);
  CHECK(strong.offense == 750);
  CHECK(strong.hp == 7500);
  // No technique: the first slot all the same.
  CHECK(strong.slots[0] == std::optional<std::size_t>{0});
  CHECK_FALSE(strong.slots[3]);

  rules.statSpread = 30;
  for (std::uint64_t seed = 1; seed < 20; ++seed) {
    Random roll{seed};
    const Rival rolled = makeRival(rules, 4, ultimate, roll);
    CHECK(rolled.offense >= 525);
    CHECK(rolled.offense <= 975);
    CHECK(rolled.hp <= 9999);
  }
}

TEST_CASE("tournament.c getCupDataJumpTable: a cup's lists through section 10's jump tables") {
  constexpr std::string_view Listing = R"(sections:
- id: 0x0005
  at: l_0010
- id: 0x000a
  at: l_0100
instructions:
- {at: 0x0010, op: end_section}
- {at: 0x0100, label: l_0100, op: jump_to, target: l_0200}
- {at: 0x0104, op: jump_to, target: l_0300}
- {at: 0x0200, label: l_0200, op: jump_to, target: l_0400}
- {at: 0x0204, op: jump_to, target: l_0410}
- {at: 0x0208, op: jump_to, target: l_0420}
- {at: 0x020c, op: jump_to, target: l_0430}
- {at: 0x0210, op: jump_to, target: l_0440}
- {at: 0x0300, label: l_0300, op: jump_to, target: l_0400}
- {at: 0x0400, label: l_0400, op: show_textbox, text: "Grade D"}
- {at: 0x0410, label: l_0410, op: data, data: "08 00 00 ff"}
- {at: 0x0420, label: l_0420, op: data, data: "0c 0d ff"}
- {at: 0x0430, label: l_0430, op: data, data: "03 04 05 fe ff"}
- {at: 0x0440, label: l_0440, op: data, data: "fe ff"}
)";
  const auto node = dwe::core::parseYaml(Listing);
  REQUIRE(node);
  const auto cup = cupDataOf(*node, 0);
  REQUIRE_MESSAGE(cup, (cup ? std::string{} : cup.error().message));
  CHECK(cup->requirements == std::vector<std::int32_t>{8, 0, 0});
  CHECK(cup->overlevel == std::vector<std::int32_t>{0x0C, 0x0D});
  CHECK(cup->eligible == std::vector<std::int32_t>{3, 4, 5});
  CHECK(cup->rivals.empty());
  CHECK_FALSE(cupDataOf(*node, 1));
  CHECK_FALSE(cupDataOf(*node, 2));
}
