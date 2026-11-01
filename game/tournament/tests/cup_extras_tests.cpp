#include <dwe/assets/file_system.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/tournament/cup_extras.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace dwe::game::tournament;

dwe::ruleset::Section originalSection() {
  const auto bytes = dwe::assets::DiskFileSystem{}.readFile(
      std::filesystem::path{DWE_GAME_PACK} / "rules" / "original" / "tournament.yaml");
  REQUIRE(bytes);
  auto node = dwe::core::parseYaml(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  REQUIRE(node);
  return {.name = "tournament", .owner = "test", .file = "tournament.yaml", .node = *node};
}

dwe::ruleset::Section sectionOf(std::string_view text) {
  auto node = dwe::core::parseYaml(text);
  REQUIRE(node);
  return {.name = "tournament", .owner = "test", .file = "tournament.yaml", .node = *node};
}

// data/tournaments' medal_ids on the disc.
constexpr std::array<std::int32_t, 6> MedalIds{0, 1, 2, 3, 4, 12};

struct World {
  std::set<std::int32_t> triggers;
  std::int32_t wins = 0;
  std::int32_t cards = 0;
  std::set<std::int32_t> medals;

  [[nodiscard]] TournamentMedalFacts facts() const {
    return {.trigger = [this](std::int32_t trigger) { return triggers.contains(trigger); },
            .tournamentWins = wins,
            .cardCopies = [this](std::int32_t card) { return card < cards ? 1 : 0; },
            .hasMedal = [this](std::int32_t medal) { return medals.contains(medal); }};
  }
};

} // namespace

TEST_CASE("tournament.yaml: the bracket's rules, no extra prizes and the medal's groups") {
  const dwe::ruleset::Section section = originalSection();
  auto bracket = parseBracketRules(section);
  REQUIRE_MESSAGE(bracket, (bracket ? std::string{} : bracket.error().message));
  REQUIRE(bracket->has_value());
  CHECK((*bracket)->framesPerSecond == 20);
  CHECK((*bracket)->soundBank == 16);
  CHECK((*bracket)->musicFont == 29);
  CHECK((*bracket)->championTrack == 2);
  CHECK((*bracket)->flash.v == 200);
  CHECK((*bracket)->pages[1] == "texture/stddat/stdtim/001");
  auto prizes = parseExtraPrizes(section);
  REQUIRE(prizes);
  CHECK(prizes->empty());
  auto medal = parseTournamentMedalRules(section);
  REQUIRE(medal);
  REQUIRE(medal->has_value());
  CHECK((*medal)->cupGroups.size() == 4);
  CHECK((*medal)->cupGroups[2] == std::pair{26, 7});
  CHECK((*medal)->linesSection == 0x4db);
}

TEST_CASE("STD_GRADES: a cup's card is its file's name in the cards' folder") {
  BracketRules rules;
  rules.cards = "texture/stddat/grade";
  const std::vector<std::string> grades{"\\STDDAT\\GRADE\\GRADED.TIM",
                                        "\\STDDAT\\GRADE\\CAPV1.TIM"};
  CHECK(bracketCardImage(rules, grades, 0) == "texture/stddat/grade/graded");
  CHECK(bracketCardImage(rules, grades, 1) == "texture/stddat/grade/capv1");
  CHECK(bracketCardImage(rules, grades, 2).empty());
  auto stadium = dwe::core::parseYaml("grades: [\"\\\\STDDAT\\\\GRADE\\\\GRADEC.TIM\"]\n");
  REQUIRE(stadium);
  CHECK(parseStadiumGrades(*stadium) == std::vector<std::string>{"\\STDDAT\\GRADE\\GRADEC.TIM"});
}

TEST_CASE("extra_prizes: a ruleset's prizes by cup") {
  auto prizes = parseExtraPrizes(sectionOf(R"(extra_prizes:
- {cup: 2, item: 40}
- {cup: 2, money: 500}
- {cup: 7, item: 3, amount: 2}
)"));
  REQUIRE(prizes);
  const std::vector<ExtraPrize> cup2 = extraPrizesOf(*prizes, 2);
  REQUIRE(cup2.size() == 2);
  CHECK(cup2[0].item == 40);
  CHECK(cup2[0].amount == 1);
  CHECK(cup2[1].money == 500);
  CHECK(extraPrizesOf(*prizes, 7)[0].amount == 2);
  CHECK(extraPrizesOf(*prizes, 0).empty());
  CHECK_FALSE(parseExtraPrizes(sectionOf("extra_prizes:\n- {cup: 1}\n")));
}

TEST_CASE("extra_prizes: the hidden content's species patches give each new form's item") {
  const auto bytes = dwe::assets::DiskFileSystem{}.readFile(
      std::filesystem::path{DWE_GAME_PACK} / "rules" / "oculto" / "species_extra.yaml");
  REQUIRE(bytes);
  auto node = dwe::core::parseYaml(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  REQUIRE(node);
  auto prizes = parseExtraPrizes(
      {.name = "species_extra", .owner = "test", .file = "species_extra.yaml", .node = *node});
  REQUIRE(prizes);
  // Animal Cup: Noble Mane; Metalic Cup: Giga Hand; Filth Cup: Metalbanana (data/items).
  for (const auto [cup, item] : {std::pair{20, 126}, std::pair{16, 125}, std::pair{17, 127}}) {
    CAPTURE(cup);
    const std::vector<ExtraPrize> found = extraPrizesOf(*prizes, cup);
    REQUIRE(found.size() == 1);
    CHECK(found[0].item == item);
  }
}

TEST_CASE("checkTournamentMedalConditions: the first group won, the wins, the cards") {
  auto medal = parseTournamentMedalRules(originalSection());
  REQUIRE(medal);
  REQUIRE(medal->has_value());
  const TournamentMedalRules& rules = **medal;
  World world;
  CHECK_FALSE(tournamentMedal(rules, MedalIds, world.facts()));
  // The second group (triggers 21 to 25) all won.
  for (std::int32_t trigger = 21; trigger <= 25; ++trigger) {
    world.triggers.insert(trigger);
  }
  CHECK(tournamentMedal(rules, MedalIds, world.facts()) == 1);
  // Its medal held: nothing, even with 100 wins (only the first condition met counts).
  world.medals.insert(1);
  world.wins = 100;
  CHECK_FALSE(tournamentMedal(rules, MedalIds, world.facts()));
  world.triggers.clear();
  CHECK(tournamentMedal(rules, MedalIds, world.facts()) == 4);
  // Every card and the game beaten: the last one, medal 12.
  world.wins = 99;
  world.cards = 66;
  CHECK_FALSE(tournamentMedal(rules, MedalIds, world.facts()));
  world.triggers.insert(50);
  CHECK(tournamentMedal(rules, MedalIds, world.facts()) == 5);
  world.medals.insert(12);
  CHECK_FALSE(tournamentMedal(rules, MedalIds, world.facts()));
}
