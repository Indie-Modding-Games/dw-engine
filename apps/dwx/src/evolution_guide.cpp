#include "evolution_guide.hpp"

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/message.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <print>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "dwx.hpp"
#include "messages.hpp"

namespace dwe::dwx {

namespace {

constexpr std::int32_t NoRequirement = -1;
// A rookie target's stat entry that marks a favoured stat (the highest stat must be one of them).
constexpr std::int32_t FavouredStat = 1;
// The table keeps HP and MP minimums in tenths: the original compares the partner's HP and MP
// divided by this.
constexpr std::int32_t HpMpDivisor = 10;
// The weight point takes the asked weight plus or minus this (rules/original/evolution.yaml).
constexpr std::int32_t WeightMargin = 5;
// 0x01: battles is a maximum; 0x10: care mistakes is a maximum (EvolutionRequirementEntry).
constexpr std::int32_t BattlesMaximumFlag = 0x01;
constexpr std::int32_t CareMaximumFlag = 0x10;
constexpr std::int32_t BabyLevel = 1;
constexpr std::int32_t RookieLevel = 3;
constexpr std::int32_t LastLevel = 5;

// The words of the guide in one language. The English ones are the game's own (pause_menu.yaml,
// evoreq.*); the Spanish ones are the Spanish game's.
struct Words {
  std::string_view code;
  std::string_view intro;
  std::string_view level;
  std::array<std::string_view, LastLevel + 1> levelNames;
  std::string_view target;
  std::string_view care;
  std::string_view weight;
  std::string_view stats;
  std::string_view bonus;
  std::string_view always;
  std::string_view noRequirement;
  std::string_view special;
  std::string_view topStat;
  std::string_view either;
  std::string_view comingFrom;
  std::string_view discipline;
  std::string_view happiness;
  std::string_view battles;
  std::string_view techs;
  std::array<std::string_view, 4> statNames; // offense, defense, speed, brain
};

constexpr Words English{
    .code = "en",
    .intro =
        "# Evolution requirements\n\n"
        "Written by `dwx evolution-guide` from the tables of your own extracted content. It is "
        "for you only: do not publish it. How to read the columns: docs/guides/evolutions.md.\n\n"
        "A target passes with 3 of the 4 groups (care mistakes, weight, stats and bonus). "
        "\"N+\" is N or more; \"0-N\", N at most. The bonus is met by any one of its "
        "conditions.\n",
    .level = "Level",
    .levelNames = {"", "Fresh", "In-Training", "Rookie", "Champion", "Ultimate"},
    .target = "Target",
    .care = "Care mistakes",
    .weight = "Weight",
    .stats = "Stats",
    .bonus = "Bonus (one of)",
    .always = "Always",
    .noRequirement = "No requirement in the table",
    .special = "Special evolution",
    .topStat = "Top stat: ",
    .either = " or ",
    .comingFrom = "From ",
    .discipline = "Discipline ",
    .happiness = "Happiness ",
    .battles = "Battles ",
    .techs = "Techs ",
    .statNames = {"Off", "Def", "Spd", "Brn"}};

constexpr Words Spanish{
    .code = "es",
    .intro =
        "# Requisitos de evolución\n\n"
        "Escrito por `dwx evolution-guide` con las tablas de tu propio contenido extraído. Es solo "
        "para ti: no lo publiques. Cómo se leen las columnas: docs/es/guides/evolutions.md.\n\n"
        "Un destino aprueba con 3 de los 4 grupos (errores de cuidado, peso, stats y extra). "
        "«N+» es N o más; «0-N», N como mucho. El extra se cumple con cualquiera de sus "
        "condiciones.\n",
    .level = "Nivel",
    .levelNames = {"", "bebé", "entrenamiento", "novato", "campeón", "definitivo"},
    .target = "Destino",
    .care = "Errores de cuidado",
    .weight = "Peso",
    .stats = "Stats",
    .bonus = "Extra (uno de)",
    .always = "Siempre",
    .noRequirement = "Sin requisito en la tabla",
    .special = "Evolución especial",
    .topStat = "El mayor: ",
    .either = " o ",
    .comingFrom = "Venir de ",
    .discipline = "Disciplina ",
    .happiness = "Felicidad ",
    .battles = "Batallas ",
    .techs = "Técnicas ",
    .statNames = {"Atq", "Def", "Vel", "Int"}};

// The words for a language code, or null when the guide has none in it.
const Words* wordsFor(std::string_view language) {
  for (const Words* words : {&English, &Spanish}) {
    if (words->code == language) {
      return words;
    }
  }
  return nullptr;
}

// A table cell: a name may not end the cell early.
std::string cell(std::string_view text) {
  std::string escaped;
  for (const char c : text) {
    if (c == '|') {
      escaped += '\\';
    }
    escaped += c;
  }
  return escaped;
}

std::string nameOf(const assets::DigimonTable& species, std::string_view id) {
  const assets::DigimonRow* row = species.find(id);
  return row != nullptr && !row->name.empty() ? row->name : std::string{id};
}

std::string atLeast(std::int32_t value) {
  return std::format("{}+", std::max(value, 0));
}

std::string upTo(std::int32_t value) {
  return std::format("0-{}", std::max(value, 0));
}

std::string statsCell(const Words& words, const assets::EvolutionRequirementEntry& entry,
                      bool favoured) {
  struct Stat {
    std::string_view name;
    std::int32_t value;
    std::int32_t divisor;
  };
  const std::array<Stat, 6> stats{
      {{.name = "HP", .value = entry.hp, .divisor = HpMpDivisor},
       {.name = "MP", .value = entry.mp, .divisor = HpMpDivisor},
       {.name = words.statNames[0], .value = entry.offense, .divisor = 1},
       {.name = words.statNames[1], .value = entry.defense, .divisor = 1},
       {.name = words.statNames[2], .value = entry.speed, .divisor = 1},
       {.name = words.statNames[3], .value = entry.brain, .divisor = 1}}};
  std::string text;
  for (const Stat& stat : stats) {
    // A minimum of 0 or none always holds.
    const bool asked = favoured ? stat.value == FavouredStat : stat.value > 0;
    if (!asked) {
      continue;
    }
    if (!text.empty()) {
      text += favoured ? std::string{words.either} : ", ";
    }
    text += favoured ? std::string{stat.name}
                     : std::format("{} {}", stat.name, atLeast(stat.value * stat.divisor));
  }
  if (text.empty()) {
    return "—";
  }
  return favoured ? std::string{words.topStat} + text : text;
}

std::string extraCell(const Words& words, const assets::EvolutionRequirementEntry& entry,
                      const assets::DigimonTable& species) {
  std::vector<std::string> parts;
  if (entry.digimon && species.find(*entry.digimon) != nullptr) {
    parts.push_back(std::string{words.comingFrom} + nameOf(species, *entry.digimon));
  }
  if (entry.discipline != NoRequirement) {
    parts.push_back(std::string{words.discipline} + atLeast(entry.discipline));
  }
  if (entry.happiness != NoRequirement) {
    parts.push_back(std::string{words.happiness} + atLeast(entry.happiness));
  }
  if (entry.battles != NoRequirement) {
    parts.push_back(std::string{words.battles} + ((entry.flags & BattlesMaximumFlag) != 0
                                                      ? upTo(entry.battles)
                                                      : atLeast(entry.battles)));
  }
  if (entry.techs != NoRequirement) {
    parts.push_back(std::string{words.techs} + atLeast(entry.techs));
  }
  std::string text;
  for (const std::string& part : parts) {
    text += text.empty() ? part : "; " + part;
  }
  return text.empty() ? "—" : text;
}

std::string targetRow(const Words& words, const assets::EvolutionTable& evolution,
                      const assets::DigimonTable& species, std::int32_t sourceLevel,
                      const assets::DigimonRow& target) {
  const std::string name = cell(nameOf(species, target.id));
  if (sourceLevel == BabyLevel) {
    return std::format("| {} | {} | | | |\n", name, words.always);
  }
  const auto entry = std::ranges::find_if(evolution.requirements, [&](const auto& requirement) {
    return requirement.target == target.id;
  });
  if (entry == evolution.requirements.end()) {
    return std::format("| {} | {} | | | |\n", name, words.noRequirement);
  }
  if (entry->care == NoRequirement && entry->weight == NoRequirement) {
    return std::format("| {} | {} | | | |\n", name, words.special);
  }
  const std::string care =
      (entry->flags & CareMaximumFlag) != 0 ? upTo(entry->care) : atLeast(entry->care);
  const std::string weight = std::format("{}-{}", std::max(entry->weight - WeightMargin, 0),
                                         std::max(entry->weight + WeightMargin, 0));
  return std::format("| {} | {} | {} | {} | {} |\n", name, care, weight,
                     cell(statsCell(words, *entry, target.level == RookieLevel)),
                     cell(extraCell(words, *entry, species)));
}

} // namespace

std::string evolutionGuide(const assets::EvolutionTable& evolution,
                           const assets::DigimonTable& species, std::string_view language) {
  const Words& words = *wordsFor(language);
  // Every species that has a chart and evolves, by level and then by its place in the table.
  std::vector<std::pair<const assets::DigimonRow*, const assets::EvolutionPathEntry*>> sources;
  for (const assets::EvolutionPathEntry& path : evolution.paths) {
    const assets::DigimonRow* row = path.digimon ? species.find(*path.digimon) : nullptr;
    if (row != nullptr && row->level >= BabyLevel && row->level < LastLevel) {
      sources.emplace_back(row, &path);
    }
  }
  std::ranges::stable_sort(sources, [](const auto& a, const auto& b) {
    return std::pair{a.first->level, a.first->index} < std::pair{b.first->level, b.first->index};
  });

  std::string text{words.intro};
  std::int32_t level = 0;
  for (const auto& [source, path] : sources) {
    if (source->level != level) {
      level = source->level;
      text += std::format("\n## {} {}: {}\n", words.level, level,
                          words.levelNames.at(static_cast<std::size_t>(level)));
    }
    std::string rows;
    for (const assets::TableReference& to : path->to) {
      const assets::DigimonRow* target = to ? species.find(*to) : nullptr;
      if (target != nullptr) {
        rows += targetRow(words, evolution, species, level, *target);
      }
    }
    if (rows.empty()) {
      continue;
    }
    text += std::format("\n### {}\n\n", nameOf(species, source->id));
    text += std::format("| {} | {} | {} | {} | {} |\n", words.target, words.care, words.weight,
                        words.stats, words.bonus);
    text += "|---|---|---|---|---|\n";
    text += rows;
  }
  return text;
}

core::Result<void> writeEvolutionGuide(const std::filesystem::path& content,
                                       const std::filesystem::path& out,
                                       std::string_view language) {
  if (wordsFor(language) == nullptr) {
    return core::fail(core::formatMessage(messages::GuideLanguage, language));
  }
  std::error_code error;
  if (std::filesystem::exists(out, error) || error) {
    return core::fail(core::formatMessage(messages::GuideOutputTaken, assets::pathText(out)));
  }
  const assets::DiskFileSystem disk;
  auto found = assets::findPacks(disk, content);
  if (!found) {
    return std::unexpected{found.error()};
  }
  auto opened = assets::openPacks(disk, found->packs, assets::PackOrigin::Engine);
  if (!opened) {
    return std::unexpected{opened.error()};
  }
  auto files = assets::VirtualFileSystem::mount(opened->locations);
  if (!files) {
    return std::unexpected{files.error()};
  }
  auto evolutionId = assets::AssetId::parse("data/evolution");
  auto speciesId = assets::AssetId::parse("data/digimon");
  if (!evolutionId || !speciesId) {
    return std::unexpected{evolutionId ? speciesId.error() : evolutionId.error()};
  }
  auto evolution = assets::loadEvolutionTable(*files, *evolutionId);
  if (!evolution) {
    return std::unexpected{evolution.error()};
  }
  auto species = assets::loadDigimonTable(*files, *speciesId);
  if (!species) {
    return std::unexpected{species.error()};
  }
  const std::string text = evolutionGuide(*evolution, *species, language);
  std::ofstream file{out, std::ios::binary};
  file << text;
  file.close();
  if (!file) {
    return core::fail(core::formatMessage(messages::GuideCannotWrite, assets::pathText(out)));
  }
  return {};
}

int evolutionGuideCommand(const std::filesystem::path& content, const std::filesystem::path& out,
                          std::string_view language) {
  if (auto written = writeEvolutionGuide(content, out, language); !written) {
    std::println(stderr, "dwx: {}", written.error().message);
    return ExitFailed;
  }
  std::println("Evolution guide: {}", assets::pathText(out));
  return ExitOk;
}

} // namespace dwe::dwx
