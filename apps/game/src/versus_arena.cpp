#include "versus_arena.hpp"

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/render/map_camera.hpp>

#include <glm/mat4x4.hpp>

#include <utility>

#include "arena_lighting.hpp"
#include "arena_view.hpp"
#include "field_clock.hpp"

namespace dwe::app {

namespace {

// btl.c: loadBattleData: the partner's species bank is in slot 4 and the rival's in slot 5
// (vs_scene.c: VS_initializeVS: loadDigimonSounds(4, ...) and loadDigimonSounds(5, ...)).
constexpr std::int64_t PartnerSoundSlot = 4;

} // namespace

core::Result<std::unique_ptr<VersusArena>> VersusArena::create(VersusArenaDesc desc) {
  if (desc.renderer == nullptr || desc.files == nullptr) {
    return core::fail("versus arena: no renderer or no packs");
  }
  auto clock = std::make_unique<ruleset::GameClock>(desc.clock);
  // The field's start map stands under the arena's stage, hidden with its characters (the
  // tournament's arena is drawn the same way over its cup's map).
  FieldSceneDesc field;
  field.clock = clock.get();
  auto scene = FieldScene::create(*desc.renderer, *desc.files, field);
  if (!scene) {
    return std::unexpected{scene.error()};
  }
  return std::unique_ptr<VersusArena>{
      new VersusArena{std::move(desc), std::move(clock), std::move(*scene)}};
}

VersusArena::VersusArena(VersusArenaDesc desc, std::unique_ptr<ruleset::GameClock> clock,
                         std::unique_ptr<FieldScene> scene)
    : desc_{std::move(desc)}, clock_{std::move(clock)}, scene_{std::move(scene)},
      shotColumns_{readShotColumns(desc_.files)} {
  const assets::DigimonTable* species = desc_.species;
  scene_->attachSpeciesRadius([species](std::string_view id) {
    const auto* row = species != nullptr ? species->find(id) : nullptr;
    return row != nullptr ? static_cast<float>(row->radius) : 0.0F;
  });
  if (desc_.audio != nullptr) {
    sound_.emplace(*desc_.audio, *desc_.files);
    sound_->attachPartner([this] { return drawn_[0]; });
    scene_->attachSound(&*sound_, [species](std::string_view id) {
      const auto* row = species != nullptr ? species->find(id) : nullptr;
      return row != nullptr
                 ? std::optional{FieldScene::SpeciesBox{.radius = static_cast<float>(row->radius),
                                                        .height = static_cast<float>(row->height)}}
                 : std::nullopt;
    });
  }
  if (auto model = assets::AssetId::parse(desc_.arena.model)) {
    scene_->setProps(*model, {glm::mat4{1.0F}});
  } else {
    core::logWarning("versus: the arena {} is not drawn: {}", desc_.arena.model,
                     model.error().message);
  }
  showStage(game::tournament::MatchCamera{desc_.arena.camera}.view(
      desc_.arena.start.partner, desc_.arena.start.rival, std::nullopt));
}

VersusArena::~VersusArena() = default;

bool VersusArena::showFighters(const game::battle::BattleState& state) {
  if (state.fighters.size() < 2) {
    return false;
  }
  const std::string& partner = state.fighters[0].combatant.species;
  const std::string& rival = state.fighters[1].combatant.species;
  bool shown = true;
  if (drawn_[0] != partner) {
    if (auto loaded = scene_->showPartnerSpecies(partner); loaded) {
      drawn_[0] = partner;
    } else {
      core::logWarning("versus: the host's fighter {} is not drawn: {}", partner,
                       loaded.error().message);
      shown = false;
    }
  }
  if (drawn_[1] != rival) {
    scene_->clearStageFighters();
    drawn_[1].clear();
    const game::battle::Point at = state.fighters[1].position;
    if (scene_->addStageFighter(RivalFighter, rival,
                                {static_cast<float>(at.x), static_cast<float>(at.z)},
                                assets::yawFromOriginal(desc_.arena.rivalYaw))) {
      drawn_[1] = rival;
    } else {
      shown = false;
    }
  }
  return shown;
}

VersusArena::Rig* VersusArena::rigOf(const game::battle::Battle& battle) {
  for (Rig& rig : rigs_) {
    if (rig.battle == &battle) {
      return &rig;
    }
  }
  return nullptr;
}

void VersusArena::prepare(game::battle::Battle& battle) {
  if (!showFighters(battle.state())) {
    core::logWarning("versus: a fighter's model is missing: its battle times its moves without "
                     "it, and the other machine may not");
  }
  Rig* rig = rigOf(battle);
  if (rig == nullptr) {
    rig = &rigs_.emplace_back(Rig{.battle = &battle, .bodies = nullptr, .flights = nullptr});
  }
  // The battle at this address is a new one: it holds none of the last one's flights.
  rig->flights.reset();
  rig->bodies =
      std::make_unique<BattleEffectBodies>(battle, scene_.get(), desc_.species, enemyNpcs_);
  // The fighters move and time their animations by their models' clips, and the techniques fly as
  // their effect scripts do (battle_screen.cpp: BattleScreen::start).
  for (game::battle::FighterId id = 0; id < battle.state().fighters.size(); ++id) {
    if (std::optional<game::battle::FighterClips> clips = rig->bodies->clips(id)) {
      battle.setClips(id, std::move(*clips));
    }
  }
  rig->flights = std::make_unique<game::battle::ScriptedFlights>(
      [files = desc_.files](std::string_view move) {
        return session::readMoveEffect(*files, move);
      },
      *rig->bodies, rig->bodies.get());
  battle.setFlights(rig->flights.get());
}

void VersusArena::battleStarted(game::battle::Battle& battle) {
  prepare(battle);
  shown_ = &battle;
  lastFrame_.reset();
  effects_.reset();
  effectFrames_ = 0;
  if (sound_) {
    // vs_scene.c: VS_initializeVS: VSLoadSounds, then each fighter's species bank.
    sound_->startBattleBank();
    const std::array<std::string, 1> rival{drawn_[1]};
    sound_->useEnemyBanks(rival);
    effects_.emplace(*desc_.files, *sound_);
    if (const Rig* rig = rigOf(battle)) {
      effects_->setBodies(*rig->bodies);
    }
  }
  // vs_combat.c: VS__combatSetup, as the tournament's STD_combatSetup: the arena's camera.
  camera_.emplace(desc_.arena.camera);
  attackFrames_ = {};
  battleFrame(battle, {});
  core::logInfo("versus: the arena shows {} against {}", drawn_[0], drawn_[1]);
}

// STD_setupMoveExecution, as StoryTournament::noteMoveStarts: a fighter that starts a technique
// may take the camera.
void VersusArena::noteMoveStarts(const game::battle::Battle& battle) {
  const game::battle::BattleState& state = battle.state();
  const std::array<game::tournament::FightFigure, 2> figures = fightFiguresOf(battle);
  for (std::size_t i = 0; i < attackFrames_.size() && i < state.fighters.size(); ++i) {
    const game::battle::Fighter& fighter = state.fighters.at(i);
    std::optional<std::int32_t>& last = attackFrames_.at(i);
    if (!fighter.attack) {
      last.reset();
      continue;
    }
    const bool started = !last || fighter.attack->frame < *last;
    last = fighter.attack->frame;
    if (!started || fighter.attack->move == battle.moves().flattenedShot()) {
      continue;
    }
    const std::optional<std::size_t> focused = camera_->focused();
    camera_->moveStarted(i,
                         static_cast<std::int32_t>(battle.moves().at(fighter.attack->move).range),
                         focused ? figures.at(*focused).moveRange : std::nullopt, cameraRolls_);
  }
}

void VersusArena::battleFrame(const game::battle::Battle& battle,
                              const game::battle::BattleEvents& events) {
  if (&battle != shown_) {
    return;
  }
  const game::battle::BattleState& state = battle.state();
  if (lastFrame_ && *lastFrame_ == state.frame) {
    return;
  }
  const bool first = !lastFrame_;
  lastFrame_ = state.frame;
  if (effects_ && !first) {
    if (const render::MapCamera* camera = scene_->camera()) {
      effects_->setView(effectViewOf(*camera));
    }
    effects_->frame(events, battle.moves());
    ++effectFrames_;
  }
  // BTL_battleTickFrame: tickObjects, the field's effects among them.
  scene_->fieldEffects().tickFrame();
  figures_ = fighterFiguresOf(state, PartnerSoundSlot);
  if (camera_ && state.fighters.size() > 1) {
    bool busy = false;
    for (const game::battle::Fighter& fighter : state.fighters) {
      busy = busy || fighter.pose == game::battle::Pose::Attacking ||
             fighter.pose == game::battle::Pose::HitReaction;
    }
    noteMoveStarts(battle);
    camera_->tickFight(busy, cameraRolls_);
    showStage(camera_->view(fightFiguresOf(battle), std::nullopt));
  }
  showBattleEffects(*scene_, effects_ ? &*effects_ : nullptr, !battle.finished(), effectFrames_);
  showBattleStatus(*scene_, &battle, desc_.species, enemyNpcs_, &shotColumns_);
}

void VersusArena::battlesOver() {
  if (shown_ == nullptr) {
    return;
  }
  shown_ = nullptr;
  if (effects_) {
    effects_->end();
  }
  effects_.reset();
  camera_.reset();
  figures_.clear();
  showBattleEffects(*scene_, nullptr, false, effectFrames_);
  showBattleStatus(*scene_, nullptr, desc_.species, enemyNpcs_, &shotColumns_);
  scene_->releaseFighters();
}

void VersusArena::showStage(const game::tournament::ShowView& view) {
  FieldStage stage;
  stage.sceneryHidden = true;
  stage.view =
      render::MapCameraDesc{.eye = worldOf(view.eye),
                            .target = worldOf(view.target),
                            .focalLength = static_cast<float>(view.projection),
                            .backgroundSize = render::OriginalScreenSize * ArenaBackgroundScale};
  stage.shownProps = {desc_.arena.model};
  stage.ground = desc_.arena.collision;
  stage.lighting = arenaLighting();
  scene_->setStage(stage);
}

void VersusArena::tick(double seconds) {
  // The fighters start each tick where they stand (PlaySession::tickSystems does the same).
  scene_->settleActors(seconds);
  if (shown_ == nullptr) {
    return;
  }
  for (std::size_t index = 0; index < figures_.size() && index < 2; ++index) {
    const std::optional<std::int32_t> npc =
        index == 0 ? std::nullopt : std::optional<std::int32_t>{RivalFighter};
    scene_->holdFighter(npc, figures_[index]);
  }
  scene_->runFighters(seconds);
}

void VersusArena::update(double frameSeconds, float alpha, const GraphicsOptions& graphics) {
  scene_->setGraphics(graphics);
  scene_->update(frameSeconds, alpha, FieldClock::Frozen);
}

} // namespace dwe::app
