#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/item_effect_table.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/evolution/evolution.hpp>
#include <dwe/game/evolution/simulation.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <ostream>
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
    if (effect.handler != assets::ItemHandler::Evolution || !effect.evolvesTo) {
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

} // namespace dwe::game::evolution
