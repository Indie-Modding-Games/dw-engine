#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/evolution_table.hpp>
#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#include "evolution_guide.hpp"

using dwe::assets::DigimonRow;
using dwe::assets::DigimonTable;
using dwe::assets::EvolutionPathEntry;
using dwe::assets::EvolutionRequirementEntry;
using dwe::assets::EvolutionTable;
using dwe::dwx::evolutionGuide;
using dwe::dwx::writeEvolutionGuide;
using dwe::test_support::TempFolder;

namespace {

DigimonRow form(std::string id, std::int32_t index, std::int32_t level, std::string name = {}) {
  DigimonRow row;
  row.id = std::move(id);
  row.index = index;
  row.level = level;
  row.name = std::move(name);
  return row;
}

EvolutionRequirementEntry noRequirement(std::string target) {
  return {.target = std::move(target),
          .index = std::nullopt,
          .digimon = std::nullopt,
          .hp = -1,
          .mp = -1,
          .offense = -1,
          .defense = -1,
          .speed = -1,
          .brain = -1,
          .care = -1,
          .weight = -1,
          .discipline = -1,
          .happiness = -1,
          .battles = -1,
          .techs = -1,
          .flags = 0};
}

EvolutionPathEntry path(std::string from, std::initializer_list<std::string> targets) {
  EvolutionPathEntry entry;
  entry.digimon = std::move(from);
  std::size_t i = 0;
  for (const std::string& target : targets) {
    entry.to.at(i++) = target;
  }
  return entry;
}

// A baby, an in-training form with two rookie targets, a rookie with a champion target and a
// special one, and an ultimate (which never evolves).
struct Synthetic {
  DigimonTable species;
  EvolutionTable evolution;
};

Synthetic synthetic() {
  Synthetic s;
  s.species.digimon = {form("species_a", 0, 1), form("species_b", 1, 2, "Form B"),
                       form("species_c", 2, 3), form("species_d", 3, 3),
                       form("species_e", 4, 4), form("species_f", 5, 4),
                       form("species_g", 6, 5)};
  auto c = noRequirement("species_c");
  c.offense = 1;
  c.defense = 1;
  c.care = 3;
  c.flags = 0x10;
  c.weight = 15;
  c.techs = 10;
  auto d = noRequirement("species_d");
  d.care = 0;
  d.weight = 3;
  auto e = noRequirement("species_e");
  // HP and MP minimums are kept in tenths.
  e.hp = 100;
  e.offense = 100;
  e.speed = 0;
  e.care = 5;
  e.weight = 20;
  e.digimon = "species_c";
  e.battles = 15;
  e.flags = 0x01;
  e.discipline = 90;
  s.evolution.requirements = {c, d, e, noRequirement("species_f")};
  // The ultimate's chart has a raw number outside the table, as the original's has.
  s.evolution.paths = {path("species_c", {"species_e", "species_f"}),
                       path("species_a", {"species_b"}),
                       path("species_b", {"species_c", "species_d"}), path("species_g", {"-2"})};
  return s;
}

std::string readText(const std::filesystem::path& file) {
  std::ifstream in{file, std::ios::binary};
  return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

} // namespace

TEST_CASE("dwx evolution-guide writes each species' targets with the four scored groups") {
  const Synthetic s = synthetic();
  const std::string text = evolutionGuide(s.evolution, s.species);

  // Levels in order, whatever the order of the charts; the ultimate has no section.
  const auto baby = text.find("## Nivel 1: bebé");
  const auto training = text.find("## Nivel 2: entrenamiento");
  const auto rookie = text.find("## Nivel 3: novato");
  REQUIRE(baby != std::string::npos);
  REQUIRE(training != std::string::npos);
  REQUIRE(rookie != std::string::npos);
  CHECK(baby < training);
  CHECK(training < rookie);
  CHECK(text.find("### species_g") == std::string::npos);
  CHECK(text.find("Nivel 5") == std::string::npos);

  // A baby's target is fixed; a name from the table replaces the id.
  CHECK(text.find("### species_a\n") != std::string::npos);
  CHECK(text.find("| Form B | Siempre | | | |") != std::string::npos);
  // A rookie target: care mistakes as a maximum, weight ± 5, the favoured stats, the moves extra.
  CHECK(text.find("### Form B\n") != std::string::npos);
  CHECK(text.find("| species_c | 0-3 | 10-20 | El mayor: Atq o Def | Técnicas 10+ |") !=
        std::string::npos);
  // No weight below 0.
  CHECK(text.find("| species_d | 0+ | 0-8 | — | — |") != std::string::npos);
  // A champion target: each minimum above 0 (HP in its units), every extra, battles as a maximum.
  CHECK(text.find("| species_e | 5+ | 15-25 | HP 1000+, Atq 100+ | Venir de species_c; "
                  "Disciplina 90+; Batallas 0-15 |") != std::string::npos);
  // No care mistakes and no weight: a special evolution, as the game's chart says.
  CHECK(text.find("| species_f | Evolución especial | | | |") != std::string::npos);
}

TEST_CASE("dwx evolution-guide never replaces a file") {
  const TempFolder folder;
  const std::filesystem::path out = folder.path() / "guide.md";
  {
    std::ofstream existing{out, std::ios::binary};
    existing << "mine";
  }
  const auto written = writeEvolutionGuide(folder.path() / "content", out);
  CHECK_FALSE(written.has_value());
  CHECK(readText(out) == "mine");
}
