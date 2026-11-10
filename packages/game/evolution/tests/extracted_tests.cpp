#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/item_effect_table.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/evolution/evolution.hpp>
#include <dwe/game/evolution/simulation.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/ruleset/species_extra.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fixture.hpp"

namespace dwe::game::evolution {

namespace {

using testing::errorOf;
using testing::valueOf;

// The extracted content with the game pack, or nothing when DWE_CONTENT is not set (scripts/dev.sh
// mounts it).
std::optional<assets::VirtualFileSystem> mountContent(const assets::DiskFileSystem& disk) {
  const std::optional<std::filesystem::path> mountedContent = dwe::test_support::userContent();
  if (!mountedContent) {
    return std::nullopt;
  }
  const std::filesystem::path& content = *mountedContent;
  auto found = assets::findPacks(disk, content);
  REQUIRE_MESSAGE(found.has_value(), errorOf(found));
  found->packs.push_back({.path = DWE_GAME_PACK, .kind = assets::FoundPack::Kind::Folder});
  const auto opened = assets::openPacks(disk, found->packs, dwe::assets::PackOrigin::Engine);
  REQUIRE_MESSAGE(opened.has_value(), errorOf(opened));
  auto vfs = assets::VirtualFileSystem::mount(opened->locations);
  REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
  return std::move(*vfs);
}

Evolution loadOriginal(const assets::VirtualFileSystem& vfs) {
  const auto ruleset = ruleset::loadRuleset(vfs, ruleset::OriginalRuleset);
  REQUIRE_MESSAGE(ruleset.has_value(), errorOf(ruleset));
  auto loaded = Evolution::load(vfs, *ruleset);
  REQUIRE_MESSAGE(loaded.has_value(), errorOf(loaded));
  return std::move(*loaded);
}

constexpr std::string_view Skipping =
    "DWE_CONTENT is not set: skipping the evolution on extracted content";
constexpr std::string_view ItemEffectsAsset = "data/item_effects";
constexpr std::string_view NoItemEffects =
    "the extracted content has no data/item_effects (extracted before dwx wrote it): skipping the evolution items";

Species idOf(const Evolution& evolution, std::string_view id) {
  const auto species = evolution.tables().find(id);
  REQUIRE_MESSAGE(species.has_value(), id);
  return valueOf(species);
}

// Random inputs of a form, over the whole range of each value.
EvolutionInputs randomInputs(Species species, ruleset::Random& random) {
  constexpr std::int32_t MaxHpMp = 10000;
  constexpr std::int32_t MaxStat = 1000;
  constexpr std::int32_t MaxWeight = 100;
  constexpr std::int32_t MaxCare = 30;
  constexpr std::int32_t MaxPercent = 101;
  constexpr std::int32_t MaxBattles = 120;
  constexpr std::int32_t MaxMoves = 60;
  constexpr std::int32_t MaxHours = 400;
  constexpr std::int32_t HappinessOffset = 100;
  constexpr std::int32_t HappinessRange = 201;
  return {.species = species,
          .stats = {.hp = random.below(MaxHpMp),
                    .mp = random.below(MaxHpMp),
                    .offense = random.below(MaxStat),
                    .defense = random.below(MaxStat),
                    .speed = random.below(MaxStat),
                    .brains = random.below(MaxStat)},
          .weight = random.below(MaxWeight),
          .careMistakes = random.below(MaxCare),
          .discipline = random.below(MaxPercent),
          .happiness = random.below(HappinessRange) - HappinessOffset,
          .tiredness = random.below(MaxPercent),
          .battles = random.below(MaxBattles),
          .learnedMoves = random.below(MaxMoves),
          .evolutionTimer = random.below(MaxHours)};
}

// Inputs of `from` that meet every requirement of `target`.
EvolutionInputs inputsMeeting(const Evolution& evolution, Species from, Species target) {
  constexpr std::int32_t HighStat = 999;
  const Requirement* requirement = evolution.tables().requirement(target);
  REQUIRE(requirement != nullptr);
  const std::int32_t divisor = evolution.rules().requirements.hpMpDivisor;
  EvolutionInputs inputs{.species = from};
  const BaseStats& minimum = requirement->minimum;
  if (evolution.tables().levelOf(target) == Level::Rookie) {
    // The highest stat must be a favoured one: the first favoured stat, alone.
    const std::array<std::pair<std::int32_t, std::int32_t BaseStats::*>, 6> stats{
        {{minimum.hp, &BaseStats::hp},
         {minimum.mp, &BaseStats::mp},
         {minimum.offense, &BaseStats::offense},
         {minimum.defense, &BaseStats::defense},
         {minimum.speed, &BaseStats::speed},
         {minimum.brains, &BaseStats::brains}}};
    for (const auto& [wanted, stat] : stats) {
      if (wanted == FavouredStat) {
        inputs.stats.*stat =
            HighStat * ((stat == &BaseStats::hp || stat == &BaseStats::mp) ? divisor : 1);
        break;
      }
    }
  } else {
    inputs.stats = {.hp = std::max(minimum.hp, 0) * divisor,
                    .mp = std::max(minimum.mp, 0) * divisor,
                    .offense = std::max(minimum.offense, 0),
                    .defense = std::max(minimum.defense, 0),
                    .speed = std::max(minimum.speed, 0),
                    .brains = std::max(minimum.brains, 0)};
  }
  inputs.careMistakes = std::max(requirement->careMistakes, 0);
  inputs.weight = requirement->weight;
  return inputs;
}

} // namespace

TEST_CASE("the user's disc, extracted: every form runs every evolution check without failing") {
  const assets::DiskFileSystem disk;
  const auto vfs = mountContent(disk);
  if (!vfs) {
    MESSAGE(Skipping);
    return;
  }
  const auto evolution = loadOriginal(*vfs);
  const EvolutionTables& tables = evolution.tables();
  REQUIRE(tables.allSpecies().size() > 60);
  constexpr std::int32_t RaisingsPerForm = 40;
  ruleset::Random random{2024};
  const PraiseScold scold{.scolded = true, .disciplineBefore = 0, .happinessBefore = -100};
  // The virus form the care section names in the original (care.toilet.virus_form).
  const std::optional<Species> virusForm = tables.find("scum");
  REQUIRE(virusForm);
  for (const Species species : tables.allSpecies()) {
    CAPTURE(tables.idOf(species));
    for (std::int32_t i = 0; i < RaisingsPerForm; ++i) {
      EvolutionInputs inputs = randomInputs(species, random);
      inputs.virusForm = virusForm;
      const EvolutionState state;
      const auto checkTarget = [&](const std::optional<EvolutionResult>& result) {
        if (result) {
          CHECK(tables.species(valueOf(result).target) != nullptr);
          // Only leaving the virus form restores the stats instead of changing them.
          CHECK((!valueOf(result).restoredStats || species == virusForm));
        }
      };
      checkTarget(evolution.checkNatural(inputs, state, CheckSpan::hourly(1), random).evolution);
      checkTarget(evolution.wakeUp(inputs, state, random));
      checkTarget(evolution.praiseOrScold(inputs, state, scold, random));
      for (const auto& [item, target] : evolution.rules().items.fixed) {
        checkTarget(evolution.useItem(inputs, state, item));
      }
      checkTarget(evolution.rebirth(inputs, random));
    }
  }
}

TEST_CASE("the user's disc, extracted: the known paths of the original") {
  const assets::DiskFileSystem disk;
  const auto vfs = mountContent(disk);
  if (!vfs) {
    MESSAGE(Skipping);
    return;
  }
  const auto evolution = loadOriginal(*vfs);
  const EvolutionTables& tables = evolution.tables();
  ruleset::Random random{5};

  // evolution.c: getFreshEvolutionTarget.
  for (const auto& [baby, inTraining] : evolution.rules().babyTargets) {
    EvolutionInputs inputs{.species = idOf(evolution, baby), .evolutionTimer = 6};
    const NaturalCheck check = evolution.checkNatural(inputs, {}, CheckSpan::hourly(1), random);
    REQUIRE_MESSAGE(check.evolution, baby);
    CHECK(valueOf(check.evolution).target == idOf(evolution, inTraining));
  }
  // The chart of the first in-training form and the first rookie.
  const auto koro = tables.pathTargets(idOf(evolution, "koro"));
  CHECK(std::vector<Species>{koro.begin(), koro.end()} ==
        std::vector<Species>{idOf(evolution, "agum"), idOf(evolution, "gabu")});
  CHECK(tables.pathTargets(idOf(evolution, "agum")).front() == idOf(evolution, "grey"));

  // The special and fixed targets have the levels of the original.
  const std::array<std::pair<std::string_view, Level>, 10> levels{{{"aird", Level::Champion},
                                                                   {"igam", Level::Champion},
                                                                   {"mono", Level::Champion},
                                                                   {"kune", Level::Rookie},
                                                                   {"sira", Level::Champion},
                                                                   {"nani", Level::Champion},
                                                                   {"veda", Level::Ultimate},
                                                                   {"scum", Level::Champion},
                                                                   {"nume", Level::Champion},
                                                                   {"ggdr", Level::Ultimate}}};
  for (const auto& [id, level] : levels) {
    CHECK_MESSAGE(tables.levelOf(idOf(evolution, id)) == level, id);
  }

  // A rookie with no candidate after the fallback hours becomes the fallback form.
  EvolutionInputs lost{.species = idOf(evolution, "agum"), .weight = 1000, .evolutionTimer = 96};
  lost.careMistakes = 1000;
  const NaturalCheck fallback = evolution.checkNatural(lost, {}, CheckSpan::hourly(1), random);
  REQUIRE(fallback.evolution);
  CHECK(valueOf(fallback.evolution).target == idOf(evolution, "nume"));
}

TEST_CASE(
    "the user's disc, extracted: every target of every chart passes with inputs that meet its requirement") {
  const assets::DiskFileSystem disk;
  const auto vfs = mountContent(disk);
  if (!vfs) {
    MESSAGE(Skipping);
    return;
  }
  const auto evolution = loadOriginal(*vfs);
  const EvolutionTables& tables = evolution.tables();
  const OriginalScoring scoring;
  const EvolutionState state;
  std::int32_t charted = 0;
  for (const Species from : tables.allSpecies()) {
    // The selection never reads the charts of the ultimates (one of them names the main character).
    const Level fromLevel = tables.levelOf(from);
    if (fromLevel == Level::None || fromLevel == Level::Ultimate) {
      continue;
    }
    for (const Species target : tables.pathTargets(from)) {
      CAPTURE(tables.idOf(from));
      CAPTURE(tables.idOf(target));
      ++charted;
      const EvolutionInputs inputs = inputsMeeting(evolution, from, target);
      const EvolutionQuery query{
          .tables = &tables, .rules = &evolution.rules(), .inputs = &inputs, .state = &state};
      CHECK(scoring.score(query, target, std::nullopt) >=
            evolution.rules().requirements.passPoints);
      // The evolution itself works; from the rookies up it teaches a move of the target's block,
      // while the in-training forms have no move in theirs and keep the basic attack.
      const auto result = evolution.evolveByScript(inputs, state, target);
      REQUIRE(result);
      const bool learns = tables.levelOf(target) >= Level::Rookie;
      CHECK(valueOf(result).learnedMove.has_value() == learns);
      CHECK((learns || valueOf(result).equippedMoves.front() == 0));
    }
  }
  CHECK(charted > 100);
}

TEST_CASE("the user's disc, extracted: a thousand simulated raisings of each in-training form") {
  const assets::DiskFileSystem disk;
  const auto vfs = mountContent(disk);
  if (!vfs) {
    MESSAGE(Skipping);
    return;
  }
  const auto evolution = loadOriginal(*vfs);
  constexpr std::int32_t Raisings = 1000;
  for (const std::string_view id : {"koro", "tuno", "toko", "tane"}) {
    const Species species = idOf(evolution, id);
    const auto generator = [species](ruleset::Random& random) {
      EvolutionInputs inputs = randomInputs(species, random);
      inputs.evolutionTimer = 24;
      return inputs;
    };
    const EvolutionHistogram histogram = simulateRaisings(evolution, generator, Raisings, 1);
    CHECK(histogram.raisings == Raisings);
    for (const auto& [target, count] : histogram.targets) {
      const auto chart = evolution.tables().pathTargets(species);
      CHECK_MESSAGE(std::ranges::find(chart, target) != chart.end(), id);
    }
    CHECK_MESSAGE(!histogram.targets.empty(), id);
  }
}

TEST_CASE(
    "the user's disc, extracted: every item of EVOLUTION_ITEM_TARGET evolves a form one level below its target") {
  const assets::DiskFileSystem disk;
  const auto vfs = mountContent(disk);
  if (!vfs) {
    MESSAGE(Skipping);
    return;
  }
  const auto itemEffectsId = assets::AssetId::parse(ItemEffectsAsset);
  REQUIRE_MESSAGE(itemEffectsId.has_value(), errorOf(itemEffectsId));
  if (vfs->resolve(*itemEffectsId) == nullptr) {
    MESSAGE(NoItemEffects);
    return;
  }
  const auto itemEffects = assets::loadItemEffectTable(*vfs);
  REQUIRE_MESSAGE(itemEffects.has_value(), errorOf(itemEffects));
  const Evolution evolution = loadOriginal(*vfs);
  const EvolutionTables& tables = evolution.tables();
  const std::vector<Species> allSpecies = tables.allSpecies();
  // item.c: EVOLUTION_ITEM_TARGET has 44 entries, for the items from 0x47 on.
  constexpr std::int32_t TableItems = 44;
  std::int32_t tableItems = 0;
  for (const assets::ItemEffectEntry& effect : itemEffects->effects) {
    // A pack's own item (no original number) is not in the table: the game pack's rare items lead
    // to forms only the hidden content's rulesets give stats to.
    if (effect.handler != assets::ItemHandler::Evolution || !effect.evolvesTo || effect.index < 0) {
      continue;
    }
    CAPTURE(effect.item);
    ++tableItems;
    const auto found = tables.itemTarget(effect.item);
    REQUIRE(found);
    const Species target = valueOf(found);
    const auto below =
        static_cast<std::int32_t>(tables.levelOf(target)) - evolution.rules().items.levelStep;
    const auto partner = std::ranges::find_if(allSpecies, [&](Species species) {
      return static_cast<std::int32_t>(tables.levelOf(species)) == below;
    });
    if (partner == allSpecies.end()) {
      continue;
    }
    const auto result = evolution.useItem({.species = *partner}, {}, effect.item);
    REQUIRE(result);
    CHECK(valueOf(result).reason == EvolutionReason::Item);
    CHECK(valueOf(result).statChange == BaseStats{});
    CHECK_FALSE(evolution.useItem({.species = target}, {}, effect.item));
  }
  CHECK(tableItems == TableItems);
}

} // namespace dwe::game::evolution

namespace dwe::game::evolution {

// The hidden content ruleset on the user's disc: its new targets sit after the original's in each
// chart, and the gated targets evolve only with their gate open.
TEST_CASE("the user's disc, extracted: the hidden content's paths and gates") {
  const assets::DiskFileSystem disk;
  const auto vfs = mountContent(disk);
  if (!vfs) {
    MESSAGE(Skipping);
    return;
  }
  const auto ruleset = ruleset::loadRuleset(*vfs, "oculto");
  REQUIRE_MESSAGE(ruleset.has_value(), errorOf(ruleset));
  auto loaded = Evolution::load(*vfs, *ruleset);
  REQUIRE_MESSAGE(loaded.has_value(), errorOf(loaded));
  const Evolution& evolution = *loaded;
  const EvolutionTables& tables = evolution.tables();
  const auto targetsOf = [&](std::string_view id) {
    const auto targets = tables.pathTargets(idOf(evolution, id));
    return std::vector<Species>{targets.begin(), targets.end()};
  };
  CHECK(targetsOf("elec").back() == idOf(evolution, "panj"));
  CHECK(targetsOf("penm").back() == idOf(evolution, "panj"));
  CHECK(targetsOf("panj") ==
        std::vector<Species>{idOf(evolution, "andr"), idOf(evolution, "mame")});
  // Devimon's last target is Myotismon, after Gigadramon.
  for (const std::string_view from : {"tyra", "devi", "aird", "sead"}) {
    const auto targets = targetsOf(from);
    CHECK_MESSAGE(std::ranges::find(targets, idOf(evolution, "ggdr")) != targets.end(), from);
  }
  CHECK(targetsOf("tyra").back() == idOf(evolution, "ggdr"));
  CHECK(targetsOf("scum").back() == idOf(evolution, "mtet"));
  CHECK(targetsOf("nume").back() == idOf(evolution, "mtet"));
  // The original's targets keep their order.
  CHECK(targetsOf("elec").front() == idOf(evolution, "leom"));

  // Panjyamon from Penguinmon: met requirement, closed then open gate.
  const Species panj = idOf(evolution, "panj");
  EvolutionInputs inputs = inputsMeeting(evolution, idOf(evolution, "penm"), panj);
  inputs.learnedMoves = 30;
  bool leomonResident = false;
  inputs.trigger = [&leomonResident](std::int32_t trigger) {
    return trigger == 248 && leomonResident;
  };
  const auto report = evolution.report(inputs, {}, panj);
  REQUIRE(report);
  CHECK(report->gateMet == false);
  leomonResident = true;
  const auto open = evolution.report(inputs, {}, panj);
  REQUIRE(open);
  CHECK(open->gateMet == true);
  CHECK(open->points >= evolution.rules().requirements.passPoints);

  // Each new target is the one the check picks from its source, with the values the route sets.
  struct Case {
    std::string_view from;
    std::string_view target;
    std::int32_t trigger;
    std::int32_t weight;
  };
  for (const Case& each : {Case{.from = "penm", .target = "panj", .trigger = 248, .weight = 25},
                           Case{.from = "sead", .target = "ggdr", .trigger = 0, .weight = 63},
                           Case{.from = "scum", .target = "mtet", .trigger = 242, .weight = 24}}) {
    CAPTURE(each.target);
    const Species target = idOf(evolution, each.target);
    EvolutionInputs meeting = inputsMeeting(evolution, idOf(evolution, each.from), target);
    meeting.weight = each.weight;
    meeting.tamerFree = true;
    meeting.evolutionTimer = 1000;
    const std::int32_t trigger = each.trigger;
    meeting.trigger = [trigger](std::int32_t set) { return set == trigger; };
    const auto scored = evolution.report(meeting, {}, target);
    REQUIRE(scored);
    CHECK(scored->points >= evolution.rules().requirements.passPoints);
    CHECK(evolution.outlook(meeting, {}).next == target);
  }

  // Myotismon from Devimon, with the route's values: only at night.
  const Species vand = idOf(evolution, "vand");
  CHECK(targetsOf("devi").back() == vand);
  CHECK(targetsOf("bake").back() == vand);
  EvolutionInputs night = inputsMeeting(evolution, idOf(evolution, "devi"), vand);
  night.weight = 30;
  night.discipline = 80;
  night.tamerFree = true;
  night.evolutionTimer = 1000;
  night.trigger = [](std::int32_t set) { return set == 129; };
  night.hourOfDay = 22;
  const auto scored = evolution.report(night, {}, vand);
  REQUIRE(scored);
  CHECK(scored->gateMet);
  CHECK(scored->points >= evolution.rules().requirements.passPoints);
  CHECK(evolution.outlook(night, {}).next == vand);
  night.hourOfDay = 12;
  CHECK(evolution.outlook(night, {}).next != vand);
}

// The Mega step on the user's disc: MetalEtemon comes from Etemon and Machinedramon from Megadramon
// and Gigadramon, each picked by an ultimate's check with the route's values.
TEST_CASE("the user's disc, extracted: the Mega step's paths") {
  const assets::DiskFileSystem disk;
  const auto vfs = mountContent(disk);
  if (!vfs) {
    MESSAGE(Skipping);
    return;
  }
  const auto ruleset = ruleset::loadRuleset(*vfs, "oculto_mega");
  REQUIRE_MESSAGE(ruleset.has_value(), errorOf(ruleset));
  auto loaded = Evolution::load(*vfs, *ruleset);
  REQUIRE_MESSAGE(loaded.has_value(), errorOf(loaded));
  const Evolution& evolution = *loaded;
  const auto targetsOf = [&](std::string_view id) {
    const auto targets = evolution.tables().pathTargets(idOf(evolution, id));
    return std::vector<Species>{targets.begin(), targets.end()};
  };
  const Species mtet = idOf(evolution, "mtet");
  const Species muge = idOf(evolution, "muge");
  CHECK(targetsOf("etem") == std::vector<Species>{mtet});
  const auto fromSukamon = targetsOf("scum");
  CHECK(std::ranges::find(fromSukamon, mtet) == fromSukamon.end());
  CHECK(targetsOf("mgdr") == std::vector<Species>{muge});
  CHECK(targetsOf("ggdr") == std::vector<Species>{muge});
  struct Case {
    std::string_view from;
    std::string_view target;
    std::int32_t trigger;
    std::int32_t weight;
  };
  for (const Case& each : {Case{.from = "etem", .target = "mtet", .trigger = 242, .weight = 20},
                           Case{.from = "mgdr", .target = "muge", .trigger = 50, .weight = 50}}) {
    CAPTURE(each.target);
    const Species target = idOf(evolution, each.target);
    EvolutionInputs meeting = inputsMeeting(evolution, idOf(evolution, each.from), target);
    meeting.weight = each.weight;
    meeting.tamerFree = true;
    const std::int32_t trigger = each.trigger;
    meeting.trigger = [trigger](std::int32_t set) { return set == trigger; };
    meeting.battles = 150;
    meeting.evolutionTimer = 120;
    CHECK(evolution.outlook(meeting, {}).checkHours == 120);
    const auto scored = evolution.report(meeting, {}, target);
    REQUIRE(scored);
    CHECK(scored->points >= evolution.rules().requirements.passPoints);
    CHECK(evolution.outlook(meeting, {}).next == target);
  }
}

} // namespace dwe::game::evolution

namespace dwe::game::evolution {

namespace {

// Every species as a partner (docs/modulos/game/evoluciones-extra.md) on the user's disc.
constexpr std::string_view AllSpeciesRuleset = "todas";
constexpr std::int32_t WitnessTries = 20000;
constexpr std::int32_t WakeUpTries = 64;
constexpr std::uint64_t WitnessSeed = 1;

Evolution loadRulesetEvolution(const assets::VirtualFileSystem& vfs, std::string_view id) {
  const auto ruleset = ruleset::loadRuleset(vfs, id);
  REQUIRE_MESSAGE(ruleset.has_value(), errorOf(ruleset));
  auto loaded = Evolution::load(vfs, *ruleset);
  REQUIRE_MESSAGE(loaded.has_value(), errorOf(loaded));
  return std::move(*loaded);
}

// Inputs of `from` near the requirement of `target`: each value meets it most of the time, and
// anything otherwise, so the other targets of the path pass or fail in every mix.
EvolutionInputs nearRequirement(const Evolution& evolution, Species from, Species target,
                                ruleset::Random& random) {
  constexpr std::int32_t Percent = 100;
  constexpr std::int32_t StatsMet = 80;
  constexpr std::int32_t ValueMet = 85;
  constexpr std::int32_t BonusMet = 60;
  constexpr std::int32_t Spread = 41;
  constexpr std::int32_t RookieStat = 61;
  constexpr std::int32_t FavouredValue = 70;
  constexpr std::int32_t AnyStat = 1000;
  constexpr std::int32_t WeightMargin = 5;
  constexpr std::int32_t AnyWeight = 80;
  constexpr std::int32_t CareSpread = 7;
  constexpr std::int32_t AnyCare = 21;
  constexpr std::int32_t BonusSpread = 11;
  constexpr std::int32_t MaxPercent = 100;
  constexpr std::int32_t AnyBattles = 201;
  constexpr std::int32_t MaxMoves = 57;
  constexpr std::int32_t Hours = 1000;
  const Requirement& need = *evolution.tables().requirement(target);
  const std::int32_t divisor = evolution.rules().requirements.hpMpDivisor;
  const bool rookie = evolution.tables().levelOf(target) == Level::Rookie;
  const std::array<std::int32_t BaseStats::*, 6> stats{&BaseStats::hp,      &BaseStats::mp,
                                                       &BaseStats::offense, &BaseStats::defense,
                                                       &BaseStats::speed,   &BaseStats::brains};
  EvolutionInputs inputs{.species = from, .evolutionTimer = Hours};
  std::vector<std::int32_t BaseStats::*> favoured;
  for (std::size_t i = 0; i < stats.size(); ++i) {
    const std::int32_t scale = i < 2 ? divisor : 1;
    const std::int32_t wanted = need.minimum.*stats.at(i);
    std::int32_t value = random.below(AnyStat);
    if (rookie) {
      value = random.below(RookieStat);
      if (wanted == FavouredStat) {
        favoured.push_back(stats.at(i));
      }
    } else if (wanted >= 0 && random.below(Percent) < StatsMet) {
      value = wanted + random.below(Spread);
    }
    inputs.stats.*stats.at(i) = value * scale;
  }
  if (!favoured.empty()) {
    const auto pick = favoured.at(
        static_cast<std::size_t>(random.below(static_cast<std::int32_t>(favoured.size()))));
    inputs.stats.*pick =
        FavouredValue * ((pick == &BaseStats::hp || pick == &BaseStats::mp) ? divisor : 1);
  }
  inputs.weight =
      need.weight >= 0 && random.below(Percent) < ValueMet
          ? std::max(1, need.weight + random.below((2 * WeightMargin) + 1) - WeightMargin)
          : 1 + random.below(AnyWeight);
  if (need.careMistakes >= 0 && random.below(Percent) < ValueMet) {
    inputs.careMistakes = need.careIsMaximum ? random.below(need.careMistakes + 1)
                                             : need.careMistakes + random.below(CareSpread);
  } else {
    inputs.careMistakes = random.below(AnyCare);
  }
  const auto bonus = [&](std::int32_t wanted, std::int32_t low, std::int32_t high) {
    return wanted >= 0 && random.below(Percent) < BonusMet
               ? std::min(high, wanted + random.below(BonusSpread))
               : low + random.below(high - low + 1);
  };
  inputs.discipline = bonus(need.discipline, 0, MaxPercent);
  inputs.happiness = bonus(need.happiness, -MaxPercent, MaxPercent);
  inputs.battles = need.battlesAreMaximum ? random.below(std::max(need.battles, 0) + 1)
                                          : bonus(need.battles, 0, AnyBattles - 1);
  inputs.learnedMoves = bonus(need.learnedMoves, 0, MaxMoves);
  return inputs;
}

// Inputs with which the periodic check of `from` picks `target`, if a search finds them.
std::optional<EvolutionInputs> witness(const Evolution& evolution, Species from, Species target) {
  if (evolution.tables().requirement(target) == nullptr) {
    return std::nullopt;
  }
  ruleset::Random random{WitnessSeed};
  for (std::int32_t attempt = 0; attempt < WitnessTries; ++attempt) {
    EvolutionInputs inputs = nearRequirement(evolution, from, target, random);
    if (evolution.outlook(inputs, {}).next == target) {
      return inputs;
    }
  }
  return std::nullopt;
}

// The species a ruleset's all_species patches add as fallback targets.
std::vector<std::string> fallbackSpecies(const assets::VirtualFileSystem& vfs) {
  const auto ruleset = ruleset::loadRuleset(vfs, AllSpeciesRuleset);
  REQUIRE_MESSAGE(ruleset.has_value(), errorOf(ruleset));
  const auto extra = ruleset::readSpeciesExtra(*ruleset);
  REQUIRE_MESSAGE(extra.has_value(), errorOf(extra));
  std::vector<std::string> ids;
  for (const auto& [id, patch] : extra->species) {
    if (patch.yields) {
      ids.push_back(id);
    }
  }
  return ids;
}

// The evolution an in-training form takes waking up on `map`, rolling until the special's roll
// passes.
std::optional<EvolutionResult> wakeUpOn(const Evolution& evolution, Species from,
                                        std::string_view map) {
  EvolutionInputs inputs{.species = from, .map = std::string{map}};
  for (std::int32_t seed = 1; seed <= WakeUpTries; ++seed) {
    ruleset::Random random{static_cast<std::uint64_t>(seed)};
    if (auto result = evolution.wakeUp(inputs, {}, random)) {
      return result;
    }
  }
  return std::nullopt;
}

} // namespace

TEST_CASE("the user's disc, extracted: every species as a partner keeps the original's picks") {
  const assets::DiskFileSystem disk;
  const auto vfs = mountContent(disk);
  if (!vfs) {
    MESSAGE(Skipping);
    return;
  }
  const Evolution original = loadOriginal(*vfs);
  const Evolution all = loadRulesetEvolution(*vfs, AllSpeciesRuleset);
  // The option off is the original: no fallback target and the original's specials only.
  for (const std::string& id : fallbackSpecies(*vfs)) {
    CAPTURE(id);
    CHECK_FALSE(original.tables().yields(idOf(original, id)));
    CHECK(all.tables().yields(idOf(all, id)));
  }
  CHECK(all.rules().specials.wakeUp.size() > original.rules().specials.wakeUp.size());
  // Each path of the original keeps its targets first, in order.
  constexpr std::int32_t Raisings = 400;
  constexpr std::uint64_t PickSeed = 23;
  ruleset::Random random{PickSeed};
  for (const Species species : original.tables().allSpecies()) {
    const auto before = original.tables().pathTargets(species);
    const auto after = all.tables().pathTargets(species);
    REQUIRE(after.size() >= before.size());
    CHECK(std::ranges::equal(before, after.first(before.size())));
    if (before.empty() || original.tables().levelOf(species) == Level::Ultimate) {
      continue;
    }
    // Whenever the original picks a form, the option picks the same one, but where the original
    // falls back to the rookie's own fallback form because no target passes: a fallback target
    // takes that place (docs/modulos/game/evoluciones-extra.md, «Prioridad del original»).
    const auto fallbackForm = original.tables().find(original.rules().fallback.rookieTarget);
    for (std::int32_t raising = 0; raising < Raisings; ++raising) {
      const EvolutionInputs inputs = randomInputs(species, random);
      const auto pick = original.outlook(inputs, {}).next;
      if (!pick) {
        continue;
      }
      const auto allPick = all.outlook(inputs, {}).next;
      CAPTURE(original.tables().idOf(species));
      if (pick == fallbackForm && allPick && all.tables().yields(*allPick)) {
        continue;
      }
      CHECK(allPick == pick);
    }
  }
}

TEST_CASE("the user's disc, extracted: every species as a partner is reached from an egg") {
  const assets::DiskFileSystem disk;
  const auto vfs = mountContent(disk);
  if (!vfs) {
    MESSAGE(Skipping);
    return;
  }
  const Evolution evolution = loadRulesetEvolution(*vfs, AllSpeciesRuleset);
  const std::vector<std::string> fallback = fallbackSpecies(*vfs);
  const auto isFallback = [&](Species species) { return evolution.tables().yields(species); };
  // From the eggs: the in-training forms, then every path step the check can take, original or
  // new, and the new wake-up specials.
  std::set<Species> reached;
  for (const auto& [baby, inTraining] : evolution.rules().babyTargets) {
    reached.insert(idOf(evolution, inTraining));
  }
  const std::size_t originalSpecials = loadOriginal(*vfs).rules().specials.wakeUp.size();
  const auto& wakeUp = evolution.rules().specials.wakeUp;
  for (std::size_t rule = originalSpecials; rule < wakeUp.size(); ++rule) {
    const SpecialRule& special = wakeUp.at(rule);
    CAPTURE(special.target);
    REQUIRE(special.requirements.size() == 1);
    for (const std::string& from : special.from) {
      const auto result =
          wakeUpOn(evolution, idOf(evolution, from), special.requirements.front().mapId);
      REQUIRE(result);
      CHECK(result->target == idOf(evolution, special.target));
    }
    // Elsewhere nothing happens.
    CHECK_FALSE(wakeUpOn(evolution, idOf(evolution, special.from.front()), "twna01"));
    reached.insert(idOf(evolution, special.target));
  }
  // Each step into or out of a new form is one the check picks with legal values.
  std::set<std::pair<Species, Species>> steps;
  for (const Species species : evolution.tables().allSpecies()) {
    for (const Species target : evolution.tables().pathTargets(species)) {
      if (!isFallback(species) && !isFallback(target)) {
        steps.emplace(species, target);
        continue;
      }
      CAPTURE(evolution.tables().idOf(species));
      CAPTURE(evolution.tables().idOf(target));
      const bool found = witness(evolution, species, target).has_value();
      CHECK(found);
      if (found) {
        steps.emplace(species, target);
      }
    }
  }
  for (bool grew = true; grew;) {
    grew = false;
    for (const auto& [from, target] : steps) {
      if (reached.contains(from) && reached.insert(target).second) {
        grew = true;
      }
    }
  }
  for (const std::string& id : fallback) {
    CAPTURE(id);
    const Species species = idOf(evolution, id);
    CHECK(reached.contains(species));
    // No new form below Ultimate is a dead end.
    if (evolution.tables().levelOf(species) != Level::Ultimate) {
      CHECK(std::ranges::any_of(steps, [&](const auto& step) { return step.first == species; }));
    }
  }
}

TEST_CASE("the user's disc, extracted: a raising along new forms from in-training to ultimate") {
  const assets::DiskFileSystem disk;
  const auto vfs = mountContent(disk);
  if (!vfs) {
    MESSAGE(Skipping);
    return;
  }
  const Evolution evolution = loadRulesetEvolution(*vfs, AllSpeciesRuleset);
  struct Line {
    std::string_view inTraining;
    std::string_view map;
    std::vector<std::string_view> forms;
  };
  // One line by a wake-up special, one by the natural check only.
  for (const Line& line :
       {Line{.inTraining = "koro", .map = "omoc01", .forms = {"toya", "tank", "tkka"}},
        Line{.inTraining = "tane", .map = "", .forms = {"otam", "geco", "wrse"}}}) {
    CAPTURE(line.inTraining);
    Species partner = idOf(evolution, line.inTraining);
    std::size_t step = 0;
    if (!line.map.empty()) {
      const auto woke = wakeUpOn(evolution, partner, line.map);
      REQUIRE(woke);
      CHECK(woke->target == idOf(evolution, line.forms.front()));
      partner = woke->target;
      step = 1;
    }
    for (; step < line.forms.size(); ++step) {
      const Species target = idOf(evolution, line.forms.at(step));
      auto inputs = witness(evolution, partner, target);
      REQUIRE(inputs);
      inputs->evolutionTimer = valueOf(evolution.checkHoursFor(partner));
      ruleset::Random random{WitnessSeed};
      const NaturalCheck check = evolution.checkNatural(*inputs, {}, CheckSpan::hourly(1), random);
      REQUIRE(check.evolution);
      CHECK(check.evolution->target == target);
      // The new form has a technique to fight with.
      CHECK(check.evolution->equippedMoves.front().has_value());
      partner = target;
    }
  }
}

} // namespace dwe::game::evolution
