#include "field_scene.hpp"

#include <dwe/animation/clip_sampling.hpp>
#include <dwe/assets/content_id.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_warps.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/game/field/rules.hpp>
#include <dwe/game/field/script_warps.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/scene/components.hpp>
#include <dwe/ui/ui.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <format>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include "battle_motion.hpp"
#include "flame_texture.hpp"
#include "messages.hpp"

namespace dwe::app {
namespace {

using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::duration<double, std::milli>;

// anim.c: startAnimation switches clips at once, for the player, the partner and the NPCs alike;
// the partner ends a clip's pass before its walk changes (partner.c: partnerTickWalking, in
// game::field::PartnerFollow), which is what keeps it from popping.
constexpr float ClipFadeSeconds = 0.0F;

constexpr std::string_view PlayerCharacter = "models/tamer";
constexpr std::string_view PartnerCharacter = "models/partner";
// A species' model is digimon/<species>/model (dwx: digimonSlug), digimon/<pack>/<name>/model for a
// species a pack adds (assets::ContentId::assetPath).
constexpr std::string_view SpeciesModelFolder = "digimon/";
constexpr std::string_view SpeciesModelName = "/model";

} // namespace

std::string speciesModelOf(std::string_view species) {
  std::string id{SpeciesModelFolder};
  id.append(assets::contentAssetPath(species)).append(SpeciesModelName);
  return id;
}

namespace {

// The species a model draws (speciesModelOf the other way round), nothing for another model.
std::optional<std::string> speciesOfModel(std::string_view model) {
  if (!model.starts_with(SpeciesModelFolder) || !model.ends_with(SpeciesModelName)) {
    return std::nullopt;
  }
  model.remove_prefix(SpeciesModelFolder.size());
  model.remove_suffix(SpeciesModelName.size());
  std::string species{model};
  if (const std::size_t slash = species.find('/'); slash != std::string::npos) {
    species[slash] = ':';
  }
  return species;
}

// anim.c: tickAnimation: a clip's sound in bank 4 is the entity's own (stats.current.vabId).
constexpr std::uint8_t SpeciesSoundBank = 4;

// The click-to-move target, drawn as a small marker on the ground while the player walks to it.
constexpr render::MapMarker TargetMarker{.feet = glm::vec3{0.0F},
                                         .width = 24.0F,
                                         .height = 24.0F,
                                         .color = glm::vec4{1.0F, 0.85F, 0.3F, 1.0F}};

// The models' light on a map (map.c: loadMapSetup): its three flat lights, and a fixed ambient of
// 0x800 (half of the GTE's 4096) whatever the map says. GsSetFlatLight takes 8-bit colours that the
// GTE holds << 4 in 4.12 fixed point, so 256 is full intensity. Directions are where the light
// travels, in the original's axes.
render::ModelLighting mapLighting(const assets::MapDocument& map) {
  constexpr float FullColor = 256.0F;
  constexpr float FixedAmbient = 0.5F;
  render::ModelLighting lighting{.lights = {}, .ambient = glm::vec3{FixedAmbient}};
  static_assert(std::tuple_size_v<decltype(map.lights)> ==
                std::tuple_size_v<decltype(lighting.lights)>);
  for (std::size_t index = 0; index < lighting.lights.size(); ++index) {
    const assets::MapLight& light = map.lights[index];
    lighting.lights[index] = {
        .direction = core::fromOriginalAxes(glm::vec3{light.direction}),
        .color = glm::vec3{static_cast<float>(light.color.r), static_cast<float>(light.color.g),
                           static_cast<float>(light.color.b)} /
                 FullColor,
    };
  }
  return lighting;
}

render::TimeOfDaySchedule scheduleOf(const game::field::TimeOfDayRules& rules) {
  return {.dayStart = rules.dayStart,
          .duskStart = rules.duskStart,
          .nightStart = rules.nightStart,
          .duskLight = rules.duskLight,
          .nightLight = rules.nightLight};
}

// The hour --time jumps the clock to: the first hour of that time of day (field rules), or for day
// the hour a new game starts at (clock rules) when it falls in the day (clock.c).
std::int32_t startHourOf(render::TimeOfDay time, const game::field::FieldRules& rules,
                         const ruleset::ClockRules& clockRules) {
  switch (time) {
  case render::TimeOfDay::Dusk:
    return rules.timeOfDay.duskStart;
  case render::TimeOfDay::Night:
    return rules.timeOfDay.nightStart;
  case render::TimeOfDay::Day:
    break;
  }
  const std::int32_t start = clockRules.start.hour;
  return render::timeOfDayAt(start, scheduleOf(rules.timeOfDay)) == render::TimeOfDay::Day
             ? start
             : rules.timeOfDay.dayStart;
}

} // namespace

void FieldScene::CameraSync::prepare(render::PrepareContext& /*context*/) {
  if (const std::optional<render::Camera>& staged = owner_->stage_.camera) {
    owner_->models_->setCamera(
        {.view = staged->view, .projection = staged->projection, .viewport = {}});
    return;
  }
  const render::MapCamera& camera = owner_->mapRenderer_->camera();
  owner_->models_->setCamera(
      {.view = camera.view(), .projection = camera.projection(), .viewport = {}});
}

FieldScene::FieldScene(const assets::VirtualFileSystem& files) : files_(&files) {}

FieldScene::~FieldScene() = default;

core::Result<std::unique_ptr<FieldScene>> FieldScene::create(render::Renderer& renderer,
                                                             const assets::VirtualFileSystem& files,
                                                             const FieldSceneDesc& desc) {
  if (desc.clock == nullptr) {
    return core::fail(std::string{messages::NoGameClock});
  }
  std::unique_ptr<FieldScene> scene{new FieldScene{files}};
  auto map = render::MapRenderer::create(renderer);
  if (!map) {
    return std::unexpected{map.error()};
  }
  scene->mapRenderer_ = std::move(*map);
  auto models = render::ModelRenderer::create(renderer);
  if (!models) {
    return std::unexpected{models.error()};
  }
  scene->models_ = std::move(*models);
  scene->mapRenderer_->setUpscaleCache(desc.upscaleCache);
  scene->models_->setUpscaleCache(desc.upscaleCache);
  auto prims = ScenePrimDrawer::create(renderer);
  if (!prims) {
    return std::unexpected{prims.error()};
  }
  scene->primDrawer_ = std::move(*prims);
  auto weather = WeatherDrawer::create(renderer);
  if (!weather) {
    return std::unexpected{weather.error()};
  }
  scene->weatherDrawer_ = std::move(*weather);
  scene->renderer_ = &renderer;
  auto grade = render::SceneGrade::create(renderer);
  if (!grade) {
    return std::unexpected{grade.error()};
  }
  scene->grade_ = std::move(*grade);
  scene->mapLights_ = loadMapLights(files);
  scene->spriteCharacterTable_ = loadSpriteCharacters(files);
  // The weather over the map and its characters, under the screen primitives (the mist, the
  // effects' flashes, the story's fades) and the interface.
  scene->drawers_ = {&scene->mapRenderer_->backgroundDrawer(),
                     &scene->mapRenderer_->groundDrawer(),
                     &scene->cameraSync_,
                     &scene->models_->opaque(),
                     &scene->mapRenderer_->layerDrawer(),
                     &scene->models_->translucent(),
                     scene->weatherDrawer_.get(),
                     scene->primDrawer_.get()};
  scene->stageDrawers_ = {&scene->cameraSync_, &scene->models_->opaque(),
                          &scene->models_->translucent(), scene->primDrawer_.get()};

  auto rules = game::field::loadFieldRules(files);
  if (!rules) {
    return core::fail(std::format("field/rules: {}", rules.error().message));
  }
  if (auto climate = game::weather::loadClimateRules(files)) {
    scene->weatherBlend_ = game::weather::WeatherBlend{climate->fadeSeconds};
    scene->seasonBlend_ = game::weather::SeasonBlend{climate->fadeSeconds};
    scene->climate_ = std::move(*climate);
  } else {
    core::logWarning("field: no weather: {}", climate.error().message);
  }
  auto warps = game::field::loadFieldWarps(files);
  if (!warps) {
    return core::fail(std::format("field/warps: {}", warps.error().message));
  }
  if (auto table = assets::loadMapTable(files)) {
    scene->mapTable_ = std::move(*table);
  } else {
    core::logWarning("field: no map table ({}); maps show their ids", table.error().message);
  }
  auto scriptWarps = game::field::loadScriptWarps(files);
  if (!scriptWarps) {
    return core::fail(std::format("field/script_warps: {}", scriptWarps.error().message));
  }
  std::string startMap = desc.startMap.value_or(warps->startMap);
  std::int32_t startExit = desc.startMap ? desc.startExit : warps->startExit;
  if (!desc.startMap && desc.startOriginalMap) {
    const assets::MapEntry* entry =
        scene->mapTable_ ? scene->mapTable_->findByOriginalIndex(*desc.startOriginalMap) : nullptr;
    if (entry != nullptr && entry->hasFile()) {
      core::logDebug("[port:story] initializeMap (map.c:3936): a new game starts on screen {} "
                     "({}) at exit {}",
                     *desc.startOriginalMap, entry->id, desc.startOriginalExit);
      startMap = entry->id;
      startExit = desc.startOriginalExit;
    } else {
      core::logWarning("field: no map of the original with index {}; the new game starts at "
                       "field/warps' start",
                       *desc.startOriginalMap);
    }
  }
  // The story's triggers, the bag and the partner, as the session keeps them; a new game's start is
  // the story's (session/story_adapter.hpp).
  scene->field_ = std::make_unique<game::field::Field>(
      scene->world_, *rules,
      game::field::ScriptWarpSetup{.warps = std::move(*scriptWarps),
                                   .facts = desc.facts,
                                   .writeTrigger = desc.writeTrigger,
                                   .unknownPolicy = game::field::VanillaUnknownPolicy,
                                   .explore = desc.explore,
                                   .hasMap = [&files](std::string_view map) {
                                     const auto id = assets::mapDataId(map);
                                     return id && files.resolve(*id) != nullptr;
                                   }});
  scene->schedule_ = scheduleOf(rules->timeOfDay);
  scene->clock_ = desc.clock;
  if (desc.time) {
    static_cast<void>(desc.clock->jumpTo(startHourOf(*desc.time, *rules, desc.clock->rules()), 0));
  }

  scene->chibiStyle_ = loadChibiStyleData(files);
  auto player = scene->loadActor(PlayerCharacter, scene->field_->playerEntity(), rules->player);
  if (!player) {
    return std::unexpected{player.error()};
  }
  scene->playerActor_ = std::move(*player);
  // A script's clip carries the player by its root motion (Field::moveByClip).
  scene->world_.registry().emplace_or_replace<game::field::CharacterClipMotions>(
      scene->field_->playerEntity(),
      std::make_shared<const assets::ClipMotions>(battleClipsOf(*scene->playerActor_->model)));
  auto partner =
      scene->loadActor(PartnerCharacter, scene->field_->partnerEntity(), rules->partner.mover);
  if (!partner) {
    return std::unexpected{partner.error()};
  }
  scene->partnerActor_ = std::move(*partner);
  scene->partnerMover_ = rules->partner.mover;
  // The partner walks by its clips' root motion (anim.c: applyRootMomentum) and waits a random
  // number of idle plays before its gesture (partner.c: randomLimit).
  scene->field_->setPartnerClips(battleClipsOf(*scene->partnerActor_->model));
  scene->field_->setPartnerRandom(
      [raw = scene.get()](std::int32_t limit) { return raw->gestureRandom_.below(limit); });
  scene->field_->setPartnerMood(scene->partnerMood());
  scene->field_->setOffScreenTest(
      [raw = scene.get()](glm::vec3 point) { return raw->offScreen(point); });

  const std::optional<assets::SpawnPoint> startAt = desc.startMap ? desc.startAt : std::nullopt;
  if (auto entered = scene->enterMap(startMap, startExit, startAt,
                                     startAt ? desc.startPartnerAt : std::nullopt);
      !entered) {
    return std::unexpected{entered.error()};
  }
  if (!startAt) {
    // main.c: main: fadeFromBlack(0x28) once the first map is in, with the player free.
    scene->field_->startPlay();
  }
  return scene;
}

core::Result<void> FieldScene::showPartnerSpecies(std::string_view species) {
  // map.c and partner.c size the partner by its species' radius in the species table.
  partnerSpeciesScale_ = speciesScale_ ? speciesScale_(species) : 1.0F;
  if (speciesRadius_) {
    field_->setPartnerRadius(speciesRadius_(species) * partnerSpeciesScale_);
  }
  auto model = assets::AssetId::parse(speciesModelOf(species));
  if (!model) {
    return std::unexpected{model.error()};
  }
  if (model->str() == partnerModel().str()) {
    return {};
  }
  const ClipStandIns standIns = speciesClips_ ? speciesClips_(species) : ClipStandIns{};
  auto partner = loadActor(PartnerCharacter, partnerActor_->entity, partnerMover_, *model,
                           MissingClips::Refuse, standIns);
  if (!partner) {
    return std::unexpected{partner.error()};
  }
  models_->remove(partnerActor_->handle);
  partnerActor_ = std::move(*partner);
  partnerClipStarted_ = false;
  field_->setPartnerClips(battleClipsOf(*partnerActor_->model));
  field_->setPartnerMood(partnerMood());
  return {};
}

core::Result<void> FieldScene::enterOriginalMap(std::int32_t index, std::int32_t exit) {
  const assets::MapEntry* entry = mapTable_ ? mapTable_->findByOriginalIndex(index) : nullptr;
  if (entry == nullptr) {
    return core::fail(std::format("no map of the original with index {}", index));
  }
  return enterMap(entry->id, exit);
}

void FieldScene::playPartnerClip(std::optional<std::uint32_t> clipId, bool moves) {
  partnerClip_ = clipId;
  partnerClipStarted_ = false;
  // The clip's root motion carries the partner as a script's clip does (Field::moveByClip); a
  // scene's clip that does not move it stops the one before.
  const bool wasMoving = std::exchange(partnerClipMoves_, moves && clipId.has_value());
  if (partnerClipMoves_ || wasMoving) {
    field_->moveByClip(partnerActor_->entity, partnerClipMoves_ ? clipId : std::nullopt);
  }
  // Back from the scene it stands again with a new wait.
  partnerLook_ = {};
  // The partner's motion picks its clip again from the next update.
  partnerActor_->motion.reset();
}

bool FieldScene::partnerClipFinished() const {
  // A clip the model lacks counts as finished, so that no scene waits for it.
  return partnerClip_ && partnerClipStarted_ &&
         (!partnerClipPlays_ || partnerActor_->player->playedThrough());
}

double FieldScene::partnerClipFramesLeft() const {
  if (!partnerClip_ || !partnerClipStarted_) {
    return std::numeric_limits<double>::infinity();
  }
  const animation::AnimationPlayer& player = *partnerActor_->player;
  const assets::AnimationClip* clip = player.clip();
  if (!partnerClipPlays_ || clip == nullptr) {
    return 0.0;
  }
  if (clip->ticksPerSecond == 0 || clip->tickCount == 0) {
    return player.playedThrough() ? 0.0 : 1.0;
  }
  // The keyframes have the counted loops unrolled, so the clip's ticks are all its frames.
  return static_cast<double>(clip->tickCount) -
         (player.time() * static_cast<double>(clip->ticksPerSecond));
}

void FieldScene::animatePartnerClip(double seconds) {
  Actor& actor = *partnerActor_;
  if (!partnerClipStarted_ && partnerClip_.has_value()) {
    partnerClipStarted_ = true;
    const std::uint32_t clip = *partnerClip_;
    partnerClipPlays_ = actor.player->play(clip);
    if (!partnerClipPlays_) {
      core::logWarning("{}", core::formatMessage(messages::PartnerClipMissing, clip,
                                                 actor.character.model.str()));
    }
    actor.player->setSpeed(1.0F);
  }
  static_cast<void>(actor.player->advance(static_cast<float>(seconds)));
}

core::Result<std::unique_ptr<FieldScene::Actor>>
FieldScene::loadActor(std::string_view characterId, scene::Entity entity,
                      const game::field::MoverRules& rules,
                      const std::optional<assets::AssetId>& otherModel, MissingClips missingClips,
                      const ClipStandIns& standIns) {
  const auto id = assets::AssetId::parse(characterId);
  if (!id) {
    return std::unexpected{id.error()};
  }
  auto character = assets::loadCharacterModel(*files_, *id);
  if (!character) {
    return core::fail(std::format("{}: {}", characterId, character.error().message));
  }
  if (otherModel) {
    character->model = *otherModel;
  } else if (character->model.str() != rules.model) {
    core::logWarning("field: {} draws {}, but field/rules moves it as {}", characterId,
                     character->model.str(), rules.model);
  }
  auto model = assets::loadModel(*files_, character->model);
  if (!model) {
    return core::fail(std::format("{}: {}", character->model.str(), model.error().message));
  }
  for (const std::uint32_t clip : assets::addClipStandIns(*model, standIns)) {
    core::logDebug("field: {} plays clip {} as {}", character->model.str(), clip,
                   standIns.at(clip));
  }
  if (missingClips == MissingClips::Drop) {
    for (const std::string& animation : assets::dropMissingClips(*character, *model)) {
      core::logDebug("[port:field] {} has no clip for '{}': drawn without it",
                     character->model.str(), animation);
    }
  } else if (auto checked = assets::checkCharacterModel(*character, *model); !checked) {
    return core::fail(std::format("{}: {}", characterId, checked.error().message));
  }
  auto handle = models_->add(*model);
  if (!handle) {
    return std::unexpected{handle.error()};
  }
  const assets::AssetSource* source = files_->resolve(character->model);
  core::logInfo("field: {} is {} from pack {}", characterId, character->model.str(),
                source != nullptr ? source->packId : "?");
  auto actor = std::make_unique<Actor>(Actor{
      .character = std::move(*character),
      .model = std::make_unique<assets::ModelData>(std::move(*model)),
      .handle = *handle,
      .player = nullptr,
      .entity = entity,
      .walkSpeed = rules.walkSpeed,
      .runSpeed = rules.runSpeed,
      .motion = std::nullopt,
      .motionClip = std::nullopt,
      .scriptedClip = std::nullopt,
      .heldClip = std::nullopt,
      .fighter = std::nullopt,
  });
  actor->player = std::make_unique<animation::AnimationPlayer>(*actor->model);
  actor->originalModel = isOriginalModel(*files_, actor->character.model);
  actor->chibi =
      chibiRigOf(actor->character.model, *actor->model, chibiStyle_, actor->originalModel);
  return actor;
}

core::Result<void> FieldScene::enterMap(const std::string& mapId, std::int32_t exit,
                                        std::optional<assets::SpawnPoint> at,
                                        std::optional<assets::SpawnPoint> partnerAt) {
  const auto start = Clock::now();
  auto scene = render::loadMapSceneData(*files_, mapId);
  if (!scene) {
    return core::fail(std::format("map {}: {}", mapId, scene.error().message));
  }
  if (mapTable_) {
    // Where each warp leads: a warp with a problem is named in the log, and disabled unless it
    // still leads to a map of the original.
    const bool basePack = assets::isBaseMapData(*files_, mapId);
    for (const assets::WarpIssue& issue :
         assets::resolveWarpTargets(scene->document, *mapTable_, basePack)) {
      const std::string_view message =
          assets::disablesWarp(issue.problem) ? messages::WarpDisabled : messages::WarpProblem;
      core::logWarning("{}", core::formatMessage(message, assets::warpIssueMessage(mapId, issue)));
    }
  }
  signsMap_ = mapId;
  signsOriginal_ = scene->images;
  signsObjects_ = scene->document.objects.value_or(assets::MapObjects{});
  signsKey_ = signsKey();
  paintSigns(scene->images);
  const auto loaded = Clock::now();
  if (at) {
    field_->enterMapAt(scene->document, *at, partnerAt);
  } else if (auto entered = field_->enterMap(scene->document, exit); !entered) {
    return core::fail(std::format("map {}: {}", mapId, entered.error().message));
  }
  const assets::MapEntry* entry = mapTable_ ? mapTable_->find(mapId) : nullptr;
  placeMapObjects(entry);
  const bool fixedTime = entry != nullptr && entry->fixedTimeOfDay;
  std::string nameKey = nameKeyOf(mapId);
  // map.c: setupMap shows the palette and the lights of the clock's time of day (updateTimeOfDay).
  clockHour_ = clock_->time().hour;
  time_ = render::timeOfDayAt(clockHour_, schedule_);
  fixedTime_ = fixedTime;
  outdoor_ = !fixedTime && scene->images.backgroundPalettes.size() > 1;
  enterWeatherArea(mapId, fixedTime);
  if (auto set = mapRenderer_->setMap(*scene, scene->backgroundVariant(time_, fixedTime)); !set) {
    return core::fail(std::format("map {}: {}", mapId, set.error().message));
  }
  standInSpriteCharacters(mapId);
  mapLighting_ = mapLighting(scene->document);
  lightingDelay_ = 0.0;
  cameraFocus_.reset();
  cameraPixel_.reset();
  minigamePrims_.clear();
  applyLighting();
  const auto done = Clock::now();

  const auto dataId = assets::mapDataId(mapId);
  const assets::AssetSource* source = dataId ? files_->resolve(*dataId) : nullptr;
  MapChangeTiming timing{.from = map_.document.id,
                         .to = mapId,
                         .loadMilliseconds = Milliseconds{loaded - start}.count(),
                         .totalMilliseconds = Milliseconds{done - start}.count()};
  const glm::vec3 spawn = field_->player().position;
  core::logInfo(
      "field: entered {} ({}) at exit {} ({:.0f}, {:.0f}) from {}; data from pack {}; loaded in {:.2f} ms, "
      "ready in {:.2f} ms",
      mapId, nameKey.empty() ? mapId : localization::tr(nameKey), exit, spawn.x, spawn.z,
      timing.from.empty() ? "-" : timing.from, source != nullptr ? source->packId : "?",
      timing.loadMilliseconds, timing.totalMilliseconds);
  if (!timing.from.empty()) {
    lastChange_ = std::move(timing);
  }
  map_ = std::move(*scene);
  // The mist's pictures are the map's; its layers carry on (overworld.c keeps them across maps).
  releaseMistTextures();
  mistShown_.reset();
  mapNameKey_ = std::move(nameKey);
  ++mapSerial_;
  arrival_ = {.exit = at ? std::nullopt : std::optional<std::int32_t>{exit}, .trigger = 0};
  // The map's objects go with it, the effects' too.
  fieldEffects_.clear();
  objectSmoke_.stop();
  return {};
}

std::string mapNameKeyOf(const assets::MapTable& table, std::string_view mapId) {
  const assets::MapEntry* entry = table.find(mapId);
  if (auto added = assets::ContentId::parse(mapId);
      entry != nullptr && added && added->isNamespaced()) {
    return localization::addedMapNameKey(*added);
  }
  if (entry == nullptr || table.displayName(*entry).empty()) {
    return {};
  }
  return localization::mapNameKey(entry->loadingName);
}

std::string FieldScene::nameKeyOf(std::string_view mapId) const {
  return mapTable_ ? mapNameKeyOf(*mapTable_, mapId) : std::string{};
}

void FieldScene::tick(const game::field::FieldInput& input, float seconds) {
  field_->setInput(input);
  field_->tick(seconds);
  // The original's object list ticks the effects once a frame with the field.
  fieldEffects_.advance(seconds, field_->rules().transition.frameSeconds);
}

void FieldScene::finishTick() {
  followClock();
  if (auto change = field_->takeMapChange()) {
    if (auto entered = enterMap(change->map, change->exit); !entered) {
      // The field waits for a map that never comes: say why, once.
      core::logError("field: {}", entered.error().message);
    } else {
      arrival_.trigger = change->trigger;
    }
  }
  followNpcs();
}

std::string FieldScene::signsKey() const {
  const localization::Catalog* catalog = localization::catalogInUse();
  if (!translatedSigns_ || catalog == nullptr) {
    return "original";
  }
  return std::format("{}|{}", catalog->language(), catalog->revision());
}

void FieldScene::paintSigns(assets::MapImages& images) {
  const localization::Catalog* catalog = localization::catalogInUse();
  if (!translatedSigns_ || catalog == nullptr) {
    return;
  }
  const signs::PaintedSigns& painted = signs_.painted(
      signsOriginal_, &signsObjects_, signsMap_, catalog->language(), catalog->revision(),
      [catalog](std::string_view key) { return catalog->text(key); });
  signs::applySignPatches(images, painted.patches, signsObjects_.sprites);
}

void FieldScene::followSigns() {
  if (signsMap_.empty() || signsKey() == signsKey_) {
    return;
  }
  signsKey_ = signsKey();
  assets::MapImages images = signsOriginal_;
  paintSigns(images);
  mapRenderer_->updateImages(images);
}

void FieldScene::playerFacePartner() {
  field_->playerFacePartner();
}

void FieldScene::followClock() {
  const std::int32_t hour = clock_->time().hour;
  if (hour == clockHour_) {
    return;
  }
  clockHour_ = hour;
  // map.c: renderMap starts the palette fade at the top of the hours where the time of day changes,
  // on maps without a fixed time; tickDaytimeTransition switches the lights when it ends.
  if (!render::timeOfDayChangesAt(hour, schedule_) ||
      render::timeOfDayAt(hour, schedule_) == time_) {
    return;
  }
  time_ = render::timeOfDayAt(hour, schedule_);
  if (!fixedTime_) {
    mapRenderer_->setVariant(map_.backgroundVariant(time_, false));
    lightingDelay_ = render::TimeOfDayFadeSeconds;
  }
}

bool FieldScene::daytimeFadeRunning() const {
  if (lightingDelay_ > 0.0) {
    return true;
  }
  const std::int32_t hour = clock_->time().hour;
  return hour != clockHour_ && !fixedTime_ && render::timeOfDayChangesAt(hour, schedule_) &&
         render::timeOfDayAt(hour, schedule_) != time_;
}

void FieldScene::applyLighting() {
  render::ModelLighting lighting = mapLighting_;
  const glm::vec3 scale = render::timeOfDayLightScale(time_, fixedTime_, schedule_);
  for (render::DirectionalLight& light : lighting.lights) {
    light.color *= scale;
  }
  models_->setLighting(lighting);
}

void FieldScene::animate(Actor& actor, double seconds) {
  const bool partner = &actor == partnerActor_.get();
  const bool followed = partner && std::exchange(partnerFollowDrawn_, false);
  if (animateFighter(actor)) {
    return;
  }
  if (actor.heldClip) {
    actor.player->setSpeed(1.0F);
    static_cast<void>(actor.player->advance(static_cast<float>(seconds)));
    return;
  }
  if (animateScripted(actor, seconds)) {
    return;
  }
  if (partner && animateFollowingPartner(actor, seconds, followed)) {
    return;
  }
  const auto& intent = world_.registry().get<scene::AnimationIntent>(actor.entity);
  const std::string_view animation =
      partner ? game::partner::nameOf(partnerAnimation(actor, intent.motion, seconds))
              : scene::motionName(intent.motion);
  std::optional<std::uint32_t> clipId = actor.character.clipOf(animation);
  if (!clipId) {
    clipId = actor.character.clipOf(scene::motionName(intent.motion));
  }
  if (!clipId) {
    return;
  }
  if (actor.motion != intent.motion || actor.motionClip != clipId) {
    core::logDebug("[port:anim] startAnimation (anim.c:201): entity {} {} (clip {}) after {:.2f} s "
                   "of the last clip",
                   static_cast<std::uint32_t>(actor.entity), animation, *clipId,
                   actor.player->time());
    actor.motion = intent.motion;
    actor.motionClip = clipId;
    actor.player->play(*clipId, ClipFadeSeconds);
  }
  float speed = intent.speedScale;
  if (const assets::AnimationClip* clip = actor.player->clip();
      clip != nullptr && intent.motion != scene::Motion::Idle) {
    const float groundSpeed =
        intent.motion == scene::Motion::Run ? actor.runSpeed : actor.walkSpeed;
    const float clipSpeed = animation::repeatGroundSpeed(*actor.model, *clip);
    if (clipSpeed > 0.0F) {
      speed *= groundSpeed / clipSpeed;
    }
  }
  actor.player->setSpeed(speed);
  // The field moves the entity with its own speeds; the clip's root motion is left out.
  static_cast<void>(actor.player->advance(static_cast<float>(seconds)));
}

void FieldScene::restOnIdle(Actor& actor, double seconds) {
  // tamer.c:520 and overworld.c:1909 (setPartnerIdling): the menu opens with startAnimation(0) on
  // the player and the partner, whatever they played, the partner's condition clips too; their
  // ticks play it on while the menu stays open (FieldClock::MenuRest).
  if (actor.heldClip || actor.scriptedClip || actor.fighter) {
    animate(actor, 0.0);
    return;
  }
  const bool partner = &actor == partnerActor_.get();
  const std::string_view idle = partner ? game::partner::nameOf(game::partner::FieldAnimation::Idle)
                                        : scene::motionName(scene::Motion::Idle);
  const std::optional<std::uint32_t> clipId = actor.character.clipOf(idle);
  if (!clipId) {
    return;
  }
  if (actor.motion != scene::Motion::Idle || actor.motionClip != clipId) {
    core::logDebug("[port:anim] setPartnerIdling (overworld.c:1909): entity {} rests on {} (clip "
                   "{}) behind the game menu",
                   static_cast<std::uint32_t>(actor.entity), idle, *clipId);
    actor.motion = scene::Motion::Idle;
    actor.motionClip = clipId;
    actor.player->play(*clipId, ClipFadeSeconds);
  }
  actor.player->setSpeed(1.0F);
  static_cast<void>(actor.player->advance(static_cast<float>(seconds)));
}

void FieldScene::playClipSounds(const Actor& actor) {
  const std::vector<assets::ClipSound> sounds = actor.player->takeSounds();
  if (sounds.empty() || sound_ == nullptr) {
    return;
  }
  const bool player = &actor == playerActor_.get();
  const bool partner = &actor == partnerActor_.get();
  // anim.c: tickAnimation plays them while isOnScreen is 1. The partner's and the NPCs' field
  // ticks work it out every frame (partner.c: partnerTickOverworld, overworld.c:
  // NPCEntityTickOverworld); the battle's (battle_tick.c) do not, so a fighter keeps its own.
  const bool shown = player || (actor.fighter ? actor.shownWhenHeld : onScreen(actor));
  if (!shown) {
    return;
  }
  for (const assets::ClipSound& cue : sounds) {
    // Bank 4 is the entity's own species bank (stats.current.vabId): slot 4 for the partner, the
    // battle's slot for a fighter (btl.c: loadBattleData) and the one an NPC kept from its last
    // battle.
    std::int64_t slot = cue.bank;
    if (cue.bank == SpeciesSoundBank) {
      slot = partner ? SpeciesSoundBank : actor.soundSlot;
    }
    core::logDebug("[port:anim] tickAnimation (anim.c:339): entity {} clip {} tick {}: "
                   "playSound({}, {})",
                   static_cast<std::uint32_t>(actor.entity), actor.player->clip()->id.value_or(0),
                   cue.tick, slot, cue.sound);
    sound_->playSound(slot, cue.sound);
  }
}

bool FieldScene::onScreen(const Actor& actor) const {
  const std::optional<std::string> species = speciesOfModel(actor.character.model.str());
  const SpeciesBox size =
      species && speciesBox_ ? speciesBox_(*species).value_or(SpeciesBox{}) : SpeciesBox{};
  const glm::vec3 at = world_.registry().get<scene::Transform>(actor.entity).position;
  constexpr std::array<std::pair<float, float>, 4> Quadrants{
      {{1.0F, 1.0F}, {1.0F, -1.0F}, {-1.0F, 1.0F}, {-1.0F, -1.0F}}};
  for (const float up : {0.0F, size.height}) {
    for (const auto& [sideX, sideZ] : Quadrants) {
      const std::optional<glm::vec2> fraction = worldToWindowFraction(
          {at.x + (size.radius * sideX), at.y + up, at.z + (size.radius * sideZ)});
      if (fraction && fraction->x >= 0.0F && fraction->x <= 1.0F && fraction->y >= 0.0F &&
          fraction->y <= 1.0F) {
        return true;
      }
    }
  }
  return false;
}

bool FieldScene::offScreen(glm::vec3 point) const {
  // map.c: isTileOffScreen: off the 320 x 240 screen by more than 40 pixels to a side (x beyond
  // +-200 and y beyond +-160 from its centre), as fractions of the window.
  constexpr float MarginX = 40.0F / 320.0F;
  constexpr float MarginY = 40.0F / 240.0F;
  if (camera() == nullptr) {
    return false;
  }
  const std::optional<glm::vec2> fraction = worldToWindowFraction(point);
  return !fraction || fraction->x < -MarginX || fraction->x > 1.0F + MarginX ||
         fraction->y < -MarginY || fraction->y > 1.0F + MarginY;
}

bool FieldScene::animateFollowingPartner(Actor& actor, double seconds, bool followed) {
  // The field's follow picks the clip and moves the partner by its root motion, frame by frame
  // (game::field::PartnerFollow); the drawing plays that clip from its start each time it starts.
  const std::optional<game::field::PartnerClip> clip = field_->partnerFollowClip();
  if (!clip) {
    return false;
  }
  const auto id = static_cast<std::uint32_t>(clip->id);
  partnerFollowDrawn_ = true;
  if (!followed && actor.motionClip == id) {
    // The follow takes over the clip the partner already plays (a care walk let go walking,
    // partner.c: tickPartnerToilet sub state 4): it goes on, not from its start.
    partnerFollowStarts_ = clip->starts;
  }
  if (actor.motionClip != id || partnerFollowStarts_ != clip->starts) {
    core::logDebug("[port:anim] startAnimation (anim.c:201): partner clip {} after {:.2f} s of "
                   "the last clip",
                   id, actor.player->time());
    actor.motion = world_.registry().get<scene::AnimationIntent>(actor.entity).motion;
    actor.motionClip = id;
    partnerFollowStarts_ = clip->starts;
    actor.player->play(id, ClipFadeSeconds);
    // Back on a picked motion later (a care walk), the look starts over.
    partnerLook_ = {};
  }
  actor.player->setSpeed(1.0F);
  static_cast<void>(actor.player->advance(static_cast<float>(seconds)));
  return true;
}

game::field::PartnerMood FieldScene::partnerMood() const {
  // partner.c: setPartnerIdle, setPartnerSlowWalking, partnerTickWalking (condition 0x10) and
  // updateConditionAnimation, by the original's animation ids the partner's model gives.
  const game::field::PartnerRules& rules = field_->rules().partner;
  const game::partner::FieldAnimation gesture = game::partner::conditionAnimation(
      partnerConditions_, partnerHappiness_,
      {.happyFrom = rules.happyGestureFrom, .unhappyBelow = rules.unhappyGestureBelow});
  std::optional<std::int32_t> gestureClip;
  if (!game::partner::isIdle(gesture) && partnerActor_) {
    if (const auto id = partnerActor_->character.clipOf(game::partner::nameOf(gesture))) {
      gestureClip = static_cast<std::int32_t>(*id);
    }
  }
  return {.tired = game::partner::idleAnimation(partnerConditions_) ==
                   game::partner::FieldAnimation::IdleTired,
          .unhappy = partnerConditions_.has(game::partner::Condition::Sulking),
          .gesture = gestureClip};
}

void FieldScene::setPartnerMood(const game::partner::Conditions& conditions,
                                std::int32_t happiness) {
  partnerConditions_ = conditions;
  partnerHappiness_ = happiness;
  field_->setPartnerMood(partnerMood());
}

game::partner::FieldAnimation FieldScene::partnerAnimation(const Actor& actor, scene::Motion motion,
                                                           double seconds) {
  using game::partner::FieldAnimation;
  PartnerLook& look = partnerLook_;
  const game::field::PartnerRules& rules = field_->rules().partner;
  const auto standAgain = [&] {
    // partner.c:907: setPartnerIdle, and a new wait of 1 to gesture_idle_plays idle plays.
    look.base = game::partner::idleAnimation(partnerConditions_);
    look.gesture.reset();
    look.standingSeconds = 0.0;
    look.idlePlays = gestureRandom_.below(rules.gestureIdlePlays) + 1;
  };
  const bool changed = look.motion != motion;
  look.motion = motion;
  if (motion != scene::Motion::Idle) {
    // The walk is picked as it starts (setPartnerSlowWalking).
    if (changed) {
      look.base = motion == scene::Motion::Run ? FieldAnimation::Run
                                               : game::partner::walkAnimation(partnerConditions_);
      look.gesture.reset();
    }
    return look.base;
  }
  if (changed) {
    standAgain();
    return look.base;
  }
  if (look.gesture) {
    // partner.c:922: a gesture that ends stands again; one that repeats goes on until it walks.
    if (actor.motionClip == actor.character.clipOf(game::partner::nameOf(*look.gesture)) &&
        actor.player->finished()) {
      standAgain();
    }
    return look.gesture.value_or(look.base);
  }
  look.standingSeconds += seconds;
  const assets::AnimationClip* idle = actor.player->clip();
  const double idleSeconds =
      idle == nullptr ? 0.0
      : idle->ticksPerSecond > 0
          ? static_cast<double>(idle->frameCount) / static_cast<double>(idle->ticksPerSecond)
          : static_cast<double>(idle->duration);
  // partner.c:916: STOP_DISTANCE_TIMER > frameCount * EMOTION_ANIM_TIMEOUT.
  if (look.standingSeconds > idleSeconds * look.idlePlays) {
    const FieldAnimation chosen = game::partner::conditionAnimation(
        partnerConditions_, partnerHappiness_,
        {.happyFrom = rules.happyGestureFrom, .unhappyBelow = rules.unhappyGestureBelow});
    // A neutral mood keeps the idle it has; a model without the gesture's clip stands on.
    if (!game::partner::isIdle(chosen) &&
        actor.character.clipOf(game::partner::nameOf(chosen)).has_value()) {
      core::logDebug("[port:partner] updateConditionAnimation (partner.c:1012): {} after {:.2f} s "
                     "standing",
                     game::partner::nameOf(chosen), look.standingSeconds);
      look.gesture = chosen;
    }
  }
  return look.gesture.value_or(look.base);
}

void FieldScene::submit(const Actor& actor, float alpha, const Look& look) {
  // model.c: renderDigimon draws no bone of a flattened fighter: its paper sprite stands in
  // (addFlatFighterBillboards).
  if (actor.fighter && actor.fighter->flatSprite) {
    return;
  }
  const scene::Pose pose = field_->drawnPose(actor.entity, alpha);
  const glm::vec3 scale = actor.fighter ? look.scale * actor.fighter->scale : look.scale;
  const glm::vec3 rise{0.0F, actor.fighter ? actor.fighter->rise : 0.0F, 0.0F};
  const glm::mat4 world =
      glm::scale(glm::rotate(glm::translate(glm::mat4{1.0F}, pose.position + rise), pose.yaw,
                             glm::vec3{0.0F, 1.0F, 0.0F}),
                 scale);
  std::span<const glm::mat4> joints = drawnJoints(actor);
  std::vector<glm::mat4> kept;
  if (look.hiddenBones != 0) {
    // A left-out bone folds to nothing (its faces collapse onto a point).
    kept.assign(joints.begin(), joints.end());
    constexpr std::size_t Bits = 64;
    for (std::size_t bone = 0; bone < kept.size() && bone < Bits; ++bone) {
      if (((look.hiddenBones >> bone) & 1U) != 0) {
        kept[bone] = glm::mat4{0.0F};
      }
    }
    joints = kept;
  }
  models_->submit({.model = actor.handle,
                   .world = world,
                   .joints = joints,
                   .tint = look.tint,
                   .lift = look.lift,
                   .additive = look.additive});
}

glm::vec4 FieldScene::sceneryTint() const {
  return {glm::vec3{1.0F - stage_.sceneryDarkness}, 1.0F};
}

void FieldScene::advanceFlames(double seconds) {
  // The original's field draws 20 frames a second (main.c: gameLoop waits VSync(3)).
  constexpr double OriginalFrameSeconds = 1.0 / 20.0;
  flameSeconds_ += seconds;
  std::optional<std::uint32_t> strip;
  while (flameSeconds_ >= OriginalFrameSeconds) {
    flameSeconds_ -= OriginalFrameSeconds;
    if (const auto shown = flameStripFrame(++flameFrame_)) {
      strip = shown;
    }
  }
  if (!strip) {
    return;
  }
  for (const std::unique_ptr<Actor>* actor : {&playerActor_, &partnerActor_}) {
    if (*actor) {
      flickerFlames(**actor, *strip);
    }
  }
  for (const std::unique_ptr<Actor>& npc : npcActors_) {
    if (npc) {
      flickerFlames(*npc, *strip);
    }
  }
  for (const auto& [id, fighter] : stageFighters_) {
    flickerFlames(*fighter, *strip);
  }
}

void FieldScene::flickerFlames(const Actor& actor, std::uint32_t strip) {
  const std::optional<std::uint32_t> width = flameStripWidth(actor.character.model.str());
  if (!width || !actor.model || actor.model->images.empty()) {
    return;
  }
  const std::vector<std::byte> pixels =
      flameStripPixels(actor.model->images.front(), *width, strip);
  if (pixels.empty()) {
    return;
  }
  if (auto updated = models_->updateImage(
          actor.handle, 0, {.x = 0, .y = 0, .width = *width, .height = FlameStripRows}, pixels);
      !updated) {
    core::logWarning("field: {}: flame strip: {}", actor.character.model.str(),
                     updated.error().message);
  }
}

void FieldScene::update(double frameSeconds, float alpha, FieldClock clock) {
  // Every frame, paused too: a language picked in the pause menu repaints the signs behind it.
  followSigns();
  const bool paused = clock != FieldClock::Running;
  const bool menuRest = clock == FieldClock::MenuRest;
  const double animationSeconds = paused ? 0.0 : frameSeconds;
  advanceFlames(animationSeconds);
  fighterSeconds_ = std::exchange(fighterSecondsDue_, 0.0);
  if (menuRest && !playerSceneClip_) {
    restOnIdle(*playerActor_, frameSeconds);
  } else {
    animate(*playerActor_, playerSceneClip_ ? frameSeconds : animationSeconds);
  }
  playClipSounds(*playerActor_);
  if (menuRest && !partnerClip_) {
    restOnIdle(*partnerActor_, frameSeconds);
    playClipSounds(*partnerActor_);
  } else if (partnerClip_) {
    // A scene's clip runs while the field waits for the scene (the care scenes: eating, sleeping,
    // praise and scold, the toilet), with its sounds as the field's.
    animatePartnerClip(frameSeconds);
    playClipSounds(*partnerActor_);
  } else {
    animate(*partnerActor_, animationSeconds);
    playClipSounds(*partnerActor_);
  }
  models_->clear();
  if (stage_.showsPlayer()) {
    submit(*playerActor_, alpha,
           {.scale = stage_.playerScale,
            .tint = sceneryTint() * glm::vec4{glm::vec3{stage_.playerFade}, 1.0F},
            .additive = stage_.playerAdditive});
  }
  if (stage_.showsNpcs()) {
    drawNpcs(animationSeconds, alpha);
    if (!mapLayerHidden_) {
      submitMapObjects();
      drawSpriteCharacters(animationSeconds);
    }
    submitProps();
  } else {
    submitStageProps();
    drawShownNpc(animationSeconds, alpha);
  }
  for (const auto& [id, actor] : stageFighters_) {
    animate(*actor, animationSeconds);
    playClipSounds(*actor);
    submit(*actor, alpha, {});
  }
  submitEffectModels();
  submitStatusModels();
  submitBattleVisualModels();
  if (stage_.showsPartner()) {
    submit(*partnerActor_, alpha,
           {.scale = stage_.partnerScale * partnerSpeciesScale_,
            .lift = stage_.partnerLift,
            .hiddenBones = stage_.partnerHiddenBones});
  }
  if (!paused) {
    advanceObjectSmoke(frameSeconds);
  }
  mapRenderer_->setShade(1.0F - stage_.sceneryDarkness);
  std::vector<ScenePrim> prims = stage_.prims;
  if (!stage_.camera && (!fieldEffects_.empty() || (hitParticles_ && !hitParticles_->empty()))) {
    std::ranges::move(drawEffects(), std::back_inserter(prims));
  }
  if (mapRenderer_->hasMap()) {
    addEffectPrims(prims);
    addBattleVisualPrims(prims);
    addFlattenedShotPrims(prims);
    addStorySprites(prims);
    addPointQuads(prims);
    prims.insert(prims.end(), minigamePrims_.begin(), minigamePrims_.end());
    // The mist (ordering-table entry 20) in front of the map's characters and effects, behind the
    // story's screen primitives (the shake's square, entry 9).
    addMist(prims, frameSeconds, paused);
    prims.insert(prims.end(), storyScreenPrims_.begin(), storyScreenPrims_.end());
  }
  primDrawer_->set(std::move(prims), mapRenderer_->camera().scale());
  const scene::Pose player = field_->drawnPose(playerActor_->entity, alpha);
  // tamer.c: checkMapInteraction calls unsetCameraFollowPlayer when a warp fires, and map.c:
  // tickCameraFollowPlayer only follows while the tamer's state is 0: the camera stays put while
  // the player walks out, and in a battle (holdCamera).
  if (const std::optional<glm::vec3> held = field_->scriptCamera()) {
    cameraFocus_ = *held;
  } else if (!cameraFocus_ || (!cameraHeld_ && !field_->mapChangePending())) {
    cameraFocus_ = player.position;
  }
  // The sprite animations wait while paused. A time-of-day fade under way does not: map.c:
  // tickDaytimeTransition only waits for GAME_STATE, which the field's pause menu leaves alone; the
  // lights switch when it ends.
  mapRenderer_->setFocusPixel(cameraPixel_);
  mapRenderer_->update(animationSeconds, frameSeconds, *cameraFocus_);
  updateWeather(frameSeconds, paused);
  if (lightingDelay_ > 0.0) {
    lightingDelay_ -= frameSeconds;
    if (lightingDelay_ <= 0.0) {
      applyLighting();
    }
  }
  std::vector<render::MapMarker> markers = extraMarkers_;
  if (field_->hasMoveTarget() && !field_->path().empty()) {
    render::MapMarker marker = TargetMarker;
    marker.feet = field_->path().back();
    markers.push_back(marker);
  }
  mapRenderer_->setMarkers(markers);
  drawGroundItems();
  drawShadows(alpha);
  gradeScene(frameSeconds);
}

void FieldScene::setPlayerClip(std::optional<std::uint32_t> clip) {
  playerSceneClip_ = false;
  Actor& actor = *playerActor_;
  if (clip == actor.heldClip) {
    return;
  }
  actor.heldClip = clip;
  if (clip) {
    if (!actor.player->play(*clip, ClipFadeSeconds)) {
      core::logWarning("{}", core::formatMessage(messages::NoPlayerClip, *clip));
    }
  } else {
    // The motion's clip starts again on the next frame.
    actor.motion.reset();
  }
}

bool FieldScene::playerClipPlaying() const {
  return playerActor_->heldClip && !playerActor_->player->finished();
}

void FieldScene::playPlayerSceneClip(std::optional<std::uint32_t> clip) {
  setPlayerClip(clip);
  playerSceneClip_ = clip.has_value();
}

bool FieldScene::playerClipPlayedThrough() const {
  return !playerActor_->heldClip || playerActor_->player->playedThrough();
}

std::optional<glm::vec3> FieldScene::playerNodePoint(std::uint32_t node, glm::vec3 offset) const {
  return nodePoint(*playerActor_, node, offset);
}

std::optional<glm::mat4> FieldScene::playerNodeTransform(std::uint32_t node) const {
  const Actor& actor = *playerActor_;
  if (!actor.model->skin) {
    return std::nullopt;
  }
  const auto joints = drawnJoints(actor);
  const auto& skin = *actor.model->skin;
  const auto found = std::ranges::find(skin.joints, node);
  const auto joint = static_cast<std::size_t>(found - skin.joints.begin());
  if (found == skin.joints.end() || joint >= joints.size() ||
      joint >= skin.inverseBindMatrices.size()) {
    return std::nullopt;
  }
  const glm::mat4 jointSpace = joints[joint] * glm::inverse(skin.inverseBindMatrices[joint]);
  const auto& transform = world_.registry().get<scene::Transform>(actor.entity);
  return glm::rotate(glm::translate(glm::mat4{1.0F}, transform.position), transform.yaw,
                     glm::vec3{0.0F, 1.0F, 0.0F}) *
         jointSpace;
}

std::span<const glm::mat4> FieldScene::drawnJoints(const Actor& actor) const {
  // The chibi style reshapes the drawing only (chibi_style.hpp).
  return drawsChibi(chibiMode_, actor.originalModel)
             ? chibiJoints_.of(*actor.model, actor.chibi, *actor.player)
             : actor.player->jointMatrices();
}

std::optional<glm::vec3> FieldScene::nodePoint(const Actor& actor, std::uint32_t node,
                                               glm::vec3 offset) const {
  if (!actor.model->skin) {
    return std::nullopt;
  }
  // Where the drawn bone is, so that an effect follows the chibi style's.
  const auto joints = drawnJoints(actor);
  const auto& skin = *actor.model->skin;
  const auto found = std::ranges::find(skin.joints, node);
  const auto joint = static_cast<std::size_t>(found - skin.joints.begin());
  if (found == skin.joints.end() || joint >= joints.size() ||
      joint >= skin.inverseBindMatrices.size()) {
    return std::nullopt;
  }
  const auto& inverseBind = skin.inverseBindMatrices;
  // A skin matrix is the joint's model-space transform times its inverse bind matrix.
  const glm::mat4 jointSpace = joints[joint] * glm::inverse(inverseBind[joint]);
  const auto& transform = world_.registry().get<scene::Transform>(actor.entity);
  const glm::mat4 world = glm::rotate(glm::translate(glm::mat4{1.0F}, transform.position),
                                      transform.yaw, glm::vec3{0.0F, 1.0F, 0.0F});
  // The original's axes: y and z point the other way.
  return glm::vec3{world * jointSpace * glm::vec4{offset.x, -offset.y, -offset.z, 1.0F}};
}

std::optional<glm::vec3> FieldScene::entityNodePoint(scene::Entity entity, std::uint32_t node,
                                                     glm::vec3 offset) const {
  if (playerActor_->entity == entity) {
    return nodePoint(*playerActor_, node, offset);
  }
  if (partnerActor_->entity == entity) {
    return nodePoint(*partnerActor_, node, offset);
  }
  const auto found = std::ranges::find(npcActors_, entity, &Actor::entity);
  if (found == npcActors_.end()) {
    return std::nullopt;
  }
  return nodePoint(**found, node, offset);
}

void FieldScene::addStorySprites(std::vector<ScenePrim>& prims) {
  const render::MapCamera& camera = mapRenderer_->camera();
  const ScenePrimFrame frame{
      .view = camera.view(), .projection = camera.projection(), .pixelScale = camera.scale()};
  for (const StorySprite& sprite : storySprites_) {
    const std::optional<rhi::TextureId> texture = stpSceneImage(sprite.image, sprite.palette);
    if (!texture || frame.depth(sprite.point) <= 0.0F) {
      continue;
    }
    // GsSortSprite: the sprite's centre (mx, my) on the point, scaled about it.
    const glm::vec4 clip = frame.clip(sprite.point);
    const glm::vec2 half = sprite.side * (sprite.scale / 2.0F);
    const glm::vec2 from = sprite.texel / sprite.imageSize;
    const glm::vec2 to = (sprite.texel + sprite.side) / sprite.imageSize;
    ScenePrim prim{.shape = ScenePrim::Shape::Quad,
                   .texture = *texture,
                   .blend = ScenePrimBlend::Additive,
                   .stpMask = true};
    prim.corners[0] = {.clip = clip, .offset = {-half.x, -half.y - sprite.lift}, .uv = from};
    prim.corners[1] = {
        .clip = clip, .offset = {half.x, -half.y - sprite.lift}, .uv = {to.x, from.y}};
    prim.corners[2] = {
        .clip = clip, .offset = {-half.x, half.y - sprite.lift}, .uv = {from.x, to.y}};
    prim.corners[3] = {.clip = clip, .offset = {half.x, half.y - sprite.lift}, .uv = to};
    prims.push_back(prim);
  }
}

void FieldScene::addPointQuads(std::vector<ScenePrim>& prims) {
  const render::MapCamera& camera = mapRenderer_->camera();
  const ScenePrimFrame frame{
      .view = camera.view(), .projection = camera.projection(), .pixelScale = camera.scale()};
  for (const PointQuad& quad : pointQuads_) {
    const std::optional<rhi::TextureId> texture = stpSceneImage(quad.image, quad.palette);
    if (!texture || frame.depth(quad.point) <= 0.0F) {
      continue;
    }
    const glm::vec4 clip = frame.clip(quad.point);
    ScenePrim prim{.shape = ScenePrim::Shape::Quad, .color = quad.color, .texture = *texture};
    for (std::size_t corner = 0; corner < prim.corners.size(); ++corner) {
      prim.corners.at(corner) = {.clip = clip,
                                 .offset = quad.offsets.at(corner),
                                 .uv = quad.texels.at(corner) / quad.imageSize};
    }
    prims.push_back(prim);
  }
}

void FieldScene::setPlayerYaw(float yaw) {
  setEntityYaw(playerActor_->entity, yaw);
}

void FieldScene::setEntityYaw(scene::Entity entity, float yaw) {
  auto* transform = world_.registry().try_get<scene::Transform>(entity);
  if (transform != nullptr) {
    transform->yaw = yaw;
    transform->previousYaw = yaw;
  }
}

void FieldScene::setStage(const FieldStage& stage) {
  stage_ = stage;
  mapRenderer_->setStageCamera(stage_.view);
}

const render::MapCamera* FieldScene::camera() const {
  return mapRenderer_->hasMap() ? &mapRenderer_->camera() : nullptr;
}

std::optional<glm::vec3> FieldScene::pickGround(glm::vec2 windowPixel) const {
  if (!mapRenderer_->hasMap()) {
    return std::nullopt;
  }
  return mapRenderer_->camera().pickGround(windowPixel);
}

std::optional<glm::vec2> FieldScene::worldToWindow(glm::vec3 world) const {
  if (!mapRenderer_->hasMap()) {
    return std::nullopt;
  }
  return mapRenderer_->camera().worldToWindow(world);
}

std::optional<glm::vec2> FieldScene::worldToWindowFraction(glm::vec3 world) const {
  const std::optional<glm::vec2> pixel = worldToWindow(world);
  if (!pixel) {
    return std::nullopt;
  }
  return *pixel / mapRenderer_->camera().viewportSize();
}

void publishPlayerPoint(ui::Ui& ui, const FieldScene& scene) {
  // The bone is about the player's waist; a lift of 120 units over the feet (an estimate, not
  // measured).
  constexpr float WaistLift = 120.0F;
  constexpr double Percent = 100.0;
  glm::vec3 point = scene.field().player().position;
  point.y += WaistLift;
  const std::optional<glm::vec2> fraction = scene.worldToWindowFraction(point);
  ui.setSetting("field.player_x", fraction ? fraction->x * Percent : 50.0);
  ui.setSetting("field.player_y", fraction ? fraction->y * Percent : 50.0);
}

} // namespace dwe::app
