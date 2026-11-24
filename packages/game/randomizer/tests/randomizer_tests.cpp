#include <dwe/core/yaml.hpp>
#include <dwe/game/randomizer/randomizer.hpp>
#include <dwe/game/randomizer/run_settings.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using dwe::game::randomizer::chaos;
using dwe::game::randomizer::LearnChance;
using dwe::game::randomizer::LearnChances;
using dwe::game::randomizer::parseCode;
using dwe::game::randomizer::Randomizer;
using dwe::game::randomizer::RandomizerRules;
using dwe::game::randomizer::readRandomizerRules;
using dwe::game::randomizer::runCode;
using dwe::game::randomizer::RunSettings;
using dwe::game::randomizer::seedText;

namespace {

constexpr std::uint64_t Seed = 0x12'3456'789AU;

const std::vector<std::string> Champions{"angemon",     "birdramon", "garurumon", "greymon",
                                         "kabuterimon", "leomon",    "meramon",   "ogremon"};
const std::vector<std::string> Items{"meat",        "medicine",         "mansion_key", "off_disk",
                                     "sm_recovery", "game:steel_heart", "various",     "def_disk"};

RandomizerRules rules() {
  return {.enemies = {{"trop02", {1, 3}}},
          .keptItems = {"mansion_key", "meat"},
          .excludedItems = {"game:steel_heart"},
          .excludedSpecies = {"leomon"}};
}

std::string fileText(const std::string& path) {
  std::ifstream file{path};
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

} // namespace

TEST_CASE("run codes carry the seed and the options and read back") {
  RunSettings run = chaos(Seed);
  run.learnChance = LearnChance::Half;
  run.drops = false;
  const std::string code = runCode(run);
  CHECK(code.size() == 11);
  CHECK(code[8] == '-');
  const auto parsed = parseCode(code);
  REQUIRE(parsed.has_value());
  CHECK(parsed->seed == Seed);
  REQUIRE(parsed->options.has_value());
  RunSettings back = *parsed->options;
  back.seed = parsed->seed;
  CHECK(back == run);
}

TEST_CASE("a typed seed ignores case, spaces and dashes and reads I, L and O as digits") {
  const std::string seed = seedText(Seed);
  std::string typed;
  for (const char character : seed) {
    typed +=
        static_cast<char>(character >= 'A' && character <= 'Z' ? character - 'A' + 'a' : character);
  }
  typed.insert(4, " - ");
  const auto parsed = parseCode(typed);
  REQUIRE(parsed.has_value());
  CHECK(parsed->seed == Seed);
  CHECK_FALSE(parsed->options.has_value());
  CHECK(parseCode("ILO00000")->seed == parseCode("11000000")->seed);
  CHECK_FALSE(parseCode("1234567").has_value());
  CHECK_FALSE(parseCode("1234567U").has_value());
  CHECK_FALSE(parseCode("12345678-ZZ").has_value());
}

TEST_CASE("an inactive run changes nothing") {
  const Randomizer randomizer{RunSettings{.seed = Seed}, rules()};
  CHECK_FALSE(randomizer.settings().active());
  CHECK_FALSE(randomizer.evolution("agumon", "greymon", Champions));
  CHECK_FALSE(randomizer.starter("agumon", Champions));
  CHECK_FALSE(randomizer.learnChance());
  CHECK_FALSE(randomizer.enemy("trop02", 1, Champions));
  CHECK_FALSE(randomizer.fieldItem("trop02", "10,12", "medicine", Items));
  CHECK_FALSE(randomizer.drop("greymon", "medicine", Items));
}

TEST_CASE("the same seed gives the same picks and another seed other ones") {
  const Randomizer first{chaos(Seed), rules()};
  const Randomizer again{chaos(Seed), rules()};
  const Randomizer other{chaos(Seed + 1), rules()};
  std::size_t differ = 0;
  for (const std::string& from : Champions) {
    for (const std::string& target : Champions) {
      const auto picked = first.evolution(from, target, Champions);
      CHECK(picked == again.evolution(from, target, Champions));
      differ += picked != other.evolution(from, target, Champions) ? 1U : 0U;
    }
  }
  CHECK(differ > Champions.size());
  CHECK(first.hash("enemy", {"trop02", "1"}) == again.hash("enemy", {"trop02", "1"}));
  CHECK(first.hash("enemy", {"trop02", "1"}) != first.hash("enemy", {"trop021", ""}));
}

TEST_CASE("the hash does not depend on the platform") {
  const Randomizer randomizer{chaos(0), {}};
  CHECK(randomizer.hash("starter", {}) == randomizer.hash("starter", {}));
  CHECK(randomizer.hash("evolution", {"agumon", "greymon"}) == 0x76FF'1872'76D3'5843U);
}

TEST_CASE("evolutions stay in the target's stage and skip excluded species") {
  const Randomizer randomizer{chaos(Seed), rules()};
  std::set<std::string> seen;
  for (const std::string& from : Champions) {
    for (const std::string& target : Champions) {
      const auto picked = randomizer.evolution(from, target, Champions);
      REQUIRE(picked.has_value());
      CHECK(std::ranges::contains(Champions, *picked));
      CHECK(*picked != "leomon");
      seen.insert(*picked);
    }
  }
  CHECK(seen.size() > 3);
}

TEST_CASE("only the listed wild slots get another enemy") {
  const Randomizer randomizer{chaos(Seed), rules()};
  CHECK(randomizer.enemy("trop02", 1, Champions).has_value());
  CHECK(randomizer.enemy("trop02", 3, Champions).has_value());
  CHECK_FALSE(randomizer.enemy("trop02", 0, Champions).has_value());
  CHECK_FALSE(randomizer.enemy("trop02", 2, Champions).has_value());
  CHECK_FALSE(randomizer.enemy("miha01", 1, Champions).has_value());
}

TEST_CASE("kept items stay where they are and never come elsewhere") {
  const Randomizer randomizer{chaos(Seed), rules()};
  CHECK_FALSE(randomizer.fieldItem("mans01", "chest200", "mansion_key", Items));
  CHECK_FALSE(randomizer.fieldItem("trop02", "4,4", "meat", Items));
  CHECK_FALSE(randomizer.drop("greymon", "mansion_key", Items));
  for (int spot = 0; spot < 200; ++spot) {
    const auto picked = randomizer.fieldItem("trop02", std::to_string(spot), "medicine", Items);
    REQUIRE(picked.has_value());
    CHECK(*picked != "mansion_key");
    CHECK(*picked != "meat");
    CHECK(*picked != "game:steel_heart");
  }
}

TEST_CASE("the learning chance is the setting's") {
  const std::array<std::optional<std::int32_t>, LearnChances.size()> percents{std::nullopt, 25, 50,
                                                                              75, 100};
  for (std::size_t index = 0; index < LearnChances.size(); ++index) {
    RunSettings run{.seed = Seed, .learnChance = LearnChances.at(index)};
    CHECK(Randomizer{run, {}}.learnChance() == percents.at(index));
  }
}

TEST_CASE("the game pack's rules list wild enemies and keep the key items") {
  auto node = dwe::core::parseYaml(
      fileText(std::string{DWE_GAME_PACK} + "/rules/original/randomizer.yaml"));
  REQUIRE(node.has_value());
  const dwe::ruleset::Section section{.name = "randomizer",
                                      .owner = "original",
                                      .file = "randomizer.yaml",
                                      .node = std::move(*node)};
  const auto read = readRandomizerRules(section);
  REQUIRE_MESSAGE(read.has_value(), (read ? std::string{} : read.error().message));
  CHECK(read->enemies.size() > 50);
  for (const std::string_view kept : {"mansion_key", "leomonstone", "as_decoder", "gear"}) {
    CHECK(std::ranges::contains(read->keptItems, kept));
  }
  CHECK(std::ranges::contains(read->excludedItems, "game:steel_heart"));
  const Randomizer randomizer{chaos(Seed), *read};
  // The roaming rares and the recruits keep their species.
  CHECK_FALSE(randomizer.wildEnemy("trop01", 0));
  CHECK_FALSE(randomizer.wildEnemy("miha01", 0));
}
