#include <dwe/animation/clip_synthesis.hpp>
#include <dwe/assets/character_model.hpp>
#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/assets/move_table.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/partner_clips.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/game/battle/simulation.hpp>
#include <dwe/game/battle/strategies.hpp>
#include <dwe/ruleset/ruleset.hpp>
#include <dwe/ruleset/species_extra.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "battle_motion.hpp"
#include "packages/game/battle/tests/fixture.hpp"

namespace {

using namespace dwe;
using namespace dwe::game::battle;
using battle_test::evenStats;
using battle_test::originalRules;

// The ruleset whose all_species section adds the character models as partners.
constexpr std::string_view AllSpeciesRuleset = "todas";
constexpr std::int32_t StatsPerLevel = 100;
constexpr std::int32_t OutOfReachMp = 9999;
constexpr std::uint64_t Seed = 7;
// The enemy both partners fight.
constexpr std::string_view Enemy = "agum";

// A fighter of a species with the first moves of its list and stats by its level (as the battle's
// disc tests make them).
Combatant speciesFighter(const assets::DigimonRow& species, const MoveCatalog& moves, Point at) {
  Combatant combatant = combatantOf(species);
  const std::int32_t value = StatsPerLevel * std::max(species.level, 1);
  combatant.stats = evenStats(value * 3, value);
  combatant.stats.maxMp = OutOfReachMp;
  combatant.hp = combatant.stats.maxHp;
  combatant.mp = combatant.stats.maxMp;
  std::size_t slot = 0;
  for (const std::optional<std::string>& move : combatant.speciesMoves) {
    if (!move || slot == FinisherSlot) {
      continue;
    }
    const auto ref = moves.find(*move);
    if (ref && !moves.at(*ref).finisher && !moves.at(*ref).counter) {
      combatant.moves.at(slot++) = *move;
    }
  }
  combatant.movePriorities = {10, 10, 10, 0};
  combatant.position = at;
  return combatant;
}

struct Fought {
  bool finished = false;
  // Each move the partner started: the frames from its start to its release, the first time.
  std::map<MoveRef, std::int64_t> releaseAfter;
};

Fought fight(const assets::DigimonRow& partner, FighterClips clips, const assets::DigimonRow& enemy,
             const MoveCatalog& moves) {
  BattleSetup setup;
  setup.partner = speciesFighter(partner, moves, {.x = 0, .z = 0});
  setup.enemies.push_back(speciesFighter(enemy, moves, {.x = 0, .z = 900}));
  setup.condition.discipline = 50;
  auto battle = Battle::start(setup, originalRules(), moves, originalStrategies(), Seed);
  REQUIRE_MESSAGE(battle.has_value(), (battle ? "" : battle.error().message));
  battle->setClips(PartnerId, std::move(clips));
  Fought fought;
  std::map<MoveRef, std::int64_t> startedAt;
  while (!battle->finished() && battle->state().frame < DefaultFrameLimit) {
    for (const BattleEvent& event : battle->step()) {
      if (event.fighter != PartnerId || !event.move) {
        continue;
      }
      if (event.kind == EventKind::AttackStarted) {
        startedAt.insert_or_assign(*event.move, battle->state().frame);
      } else if (event.kind == EventKind::AttackReleased && startedAt.contains(*event.move)) {
        fought.releaseAfter.try_emplace(*event.move,
                                        battle->state().frame - startedAt.at(*event.move));
      }
    }
  }
  fought.finished = battle->finished();
  return fought;
}

} // namespace

// Runs only when DWE_CONTENT names a content folder (scripts/dev.sh mounts it). Each character
// model of the partner clip book, with the ruleset's patches (its level, its donor's move row) and
// its made clips, fights to an end; its battle clips move as its donor's, and every technique both
// use leaves it on the same frame as the donor's does.
TEST_CASE("the user's disc, extracted: each character model fights with its partner clips") {
  const std::optional<std::filesystem::path> content = test_support::userContent();
  if (!content) {
    MESSAGE("DWE_CONTENT is not set: skipping the test on the extracted content");
    return;
  }
  const assets::DiskFileSystem disk;
  auto packs = assets::findPacks(disk, *content);
  REQUIRE(packs.has_value());
  packs->packs.push_back({.path = DWE_GAME_PACK, .kind = assets::FoundPack::Kind::Folder});
  const auto opened = assets::openPacks(disk, packs->packs, assets::PackOrigin::Engine);
  REQUIRE_MESSAGE(opened.has_value(), (opened ? "" : opened.error().message));
  const auto vfs = assets::VirtualFileSystem::mount(opened->locations);
  REQUIRE_MESSAGE(vfs.has_value(), (vfs ? "" : vfs.error().message));

  const auto rules = ruleset::loadRuleset(*vfs, AllSpeciesRuleset);
  REQUIRE_MESSAGE(rules.has_value(), (rules ? "" : rules.error().message));
  const auto species = ruleset::loadPatchedDigimonTable(*vfs, *rules);
  REQUIRE_MESSAGE(species.has_value(), (species ? "" : species.error().message));
  const auto moveTable = assets::loadMoveTable(*vfs, *assets::AssetId::parse("data/moves"));
  REQUIRE(moveTable.has_value());
  const auto moves = MoveCatalog::build(*moveTable, originalRules());
  REQUIRE(moves.has_value());
  const auto book =
      assets::loadPartnerClipBook(*vfs, *assets::AssetId::parse(assets::PartnerClipBookAsset));
  REQUIRE_MESSAGE(book.has_value(), (book ? "" : book.error().message));
  const auto partner = assets::loadCharacterModel(*vfs, *assets::AssetId::parse("models/partner"));
  REQUIRE(partner.has_value());
  // The species' models sit beside the starting partner's, one folder each.
  const std::filesystem::path speciesFolder =
      std::filesystem::path{partner->model.str()}.parent_path().parent_path();
  const auto modelOf = [&](const std::string& id) {
    const auto asset = assets::AssetId::parse((speciesFolder / id / "model").generic_string());
    REQUIRE(asset.has_value());
    auto model = assets::loadModel(*vfs, *asset);
    REQUIRE_MESSAGE(model.has_value(), (model ? "" : model.error().message));
    return std::move(*model);
  };
  const assets::DigimonRow* enemy = species->find(Enemy);
  REQUIRE(enemy != nullptr);

  for (const auto& [id, recipe] : book->species) {
    CAPTURE(id);
    const assets::DigimonRow* row = species->find(id);
    const assets::DigimonRow* donorRow = species->find(recipe.donor);
    REQUIRE(row != nullptr);
    REQUIRE(donorRow != nullptr);
    // A partner form: its level and its donor's techniques, slot by slot.
    CHECK(row->level > 0);
    CHECK(row->moves == donorRow->moves);

    assets::ModelData model = modelOf(id);
    const assets::ModelData donor = modelOf(recipe.donor);
    REQUIRE(animation::addPartnerClips(model, donor, *book, recipe).has_value());
    const FighterClips clips = app::battleClipsOf(model);
    const FighterClips donorClips = app::battleClipsOf(donor);
    for (const auto& [clip, motion] : donorClips) {
      if (clip < 33) {
        continue;
      }
      CAPTURE(clip);
      REQUIRE(clips.contains(clip));
      CHECK(clips.at(clip).steps == motion.steps);
      CHECK(clips.at(clip).repeatFrom == motion.repeatFrom);
    }

    const Fought own = fight(*row, clips, *enemy, *moves);
    const Fought asDonor = fight(*donorRow, donorClips, *enemy, *moves);
    CHECK(own.finished);
    CHECK_FALSE(own.releaseAfter.empty());
    for (const auto& [move, frames] : own.releaseAfter) {
      CAPTURE(moves->at(move).id);
      if (const auto donorFrames = asDonor.releaseAfter.find(move);
          donorFrames != asDonor.releaseAfter.end()) {
        CHECK(frames == donorFrames->second);
      }
    }
  }
}
