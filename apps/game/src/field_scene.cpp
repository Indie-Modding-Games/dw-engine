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

#include <algorithm>
#include <chrono>
#include <format>
#include <tuple>
#include <utility>

#include "messages.hpp"

namespace dwe::app {
namespace {

using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::duration<double, std::milli>;

// The original switches clips at once; a short blend hides the pop when a walk turns into a run.
constexpr float ClipFadeSeconds = 0.1F;

constexpr std::string_view PlayerCharacter = "models/tamer";
constexpr std::string_view PartnerCharacter = "models/partner";
// A species' model is digimon/<species>/model (dwx: digimonSlug), digimon/<pack>/<name>/model for a
// species a pack adds (assets::ContentId::assetPath).
constexpr std::string_view SpeciesModelFolder = "digimon/";
constexpr std::string_view SpeciesModelName = "/model";

std::string speciesModelOf(std::string_view species) {
  std::string id{SpeciesModelFolder};
  id.append(assets::contentAssetPath(species)).append(SpeciesModelName);
  return id;
}

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
  scene->drawers_ = {&scene->mapRenderer_->backgroundDrawer(), &scene->cameraSync_,
                     &scene->models_->opaque(), &scene->mapRenderer_->layerDrawer(),
                     &scene->models_->translucent()};
  scene->stageDrawers_ = {&scene->cameraSync_, &scene->models_->opaque(),
                          &scene->models_->translucent()};

  auto rules = game::field::loadFieldRules(files);
  if (!rules) {
    return core::fail(std::format("field/rules: {}", rules.error().message));
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
  const std::string startMap = desc.startMap.value_or(warps->startMap);
  const std::int32_t startExit = desc.startMap ? desc.startExit : warps->startExit;
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

  auto player = scene->loadActor(PlayerCharacter, scene->field_->playerEntity(), rules->player);
  if (!player) {
    return std::unexpected{player.error()};
  }
  scene->playerActor_ = std::move(*player);
  auto partner =
      scene->loadActor(PartnerCharacter, scene->field_->partnerEntity(), rules->partner.mover);
  if (!partner) {
    return std::unexpected{partner.error()};
  }
  scene->partnerActor_ = std::move(*partner);
  scene->partnerMover_ = rules->partner.mover;

  if (auto entered =
          scene->enterMap(startMap, startExit, desc.startMap ? desc.startAt : std::nullopt);
      !entered) {
    return std::unexpected{entered.error()};
  }
  return scene;
}

core::Result<void> FieldScene::showPartnerSpecies(std::string_view species) {
  auto model = assets::AssetId::parse(speciesModelOf(species));
  if (!model) {
    return std::unexpected{model.error()};
  }
  if (model->str() == partnerModel().str()) {
    return {};
  }
  auto partner = loadActor(PartnerCharacter, partnerActor_->entity, partnerMover_, *model);
  if (!partner) {
    return std::unexpected{partner.error()};
  }
  models_->remove(partnerActor_->handle);
  partnerActor_ = std::move(*partner);
  partnerClipStarted_ = false;
  return {};
}

void FieldScene::playPartnerClip(std::optional<std::uint32_t> clipId) {
  partnerClip_ = clipId;
  partnerClipStarted_ = false;
  // The partner's motion picks its clip again from the next update.
  partnerActor_->motion.reset();
}

bool FieldScene::partnerClipFinished() const {
  // A clip the model lacks counts as finished, so that no scene waits for it.
  return partnerClip_ && partnerClipStarted_ &&
         (!partnerClipPlays_ || partnerActor_->player->finished());
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
                      const std::optional<assets::AssetId>& otherModel) {
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
  if (auto checked = assets::checkCharacterModel(*character, *model); !checked) {
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
      .scriptedClip = std::nullopt,
  });
  actor->player = std::make_unique<animation::AnimationPlayer>(*actor->model);
  return actor;
}

core::Result<void> FieldScene::enterMap(const std::string& mapId, std::int32_t exit,
                                        std::optional<assets::SpawnPoint> at) {
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
  const auto loaded = Clock::now();
  if (at) {
    field_->enterMapAt(scene->document, *at);
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
  if (auto set = mapRenderer_->setMap(*scene, scene->backgroundVariant(time_, fixedTime)); !set) {
    return core::fail(std::format("map {}: {}", mapId, set.error().message));
  }
  mapLighting_ = mapLighting(scene->document);
  lightingDelay_ = 0.0;
  cameraFocus_.reset();
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
  mapNameKey_ = std::move(nameKey);
  ++mapSerial_;
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
}

void FieldScene::finishTick() {
  followClock();
  if (auto change = field_->takeMapChange()) {
    if (auto entered = enterMap(change->map, change->exit); !entered) {
      // The field waits for a map that never comes: say why, once.
      core::logError("field: {}", entered.error().message);
    }
  }
  followNpcs();
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

void FieldScene::applyLighting() {
  render::ModelLighting lighting = mapLighting_;
  const glm::vec3 scale = render::timeOfDayLightScale(time_, fixedTime_, schedule_);
  for (render::DirectionalLight& light : lighting.lights) {
    light.color *= scale;
  }
  models_->setLighting(lighting);
}

void FieldScene::animate(Actor& actor, double seconds) {
  if (animateScripted(actor, seconds)) {
    return;
  }
  const auto& intent = world_.registry().get<scene::AnimationIntent>(actor.entity);
  const std::optional<std::uint32_t> clipId =
      actor.character.clipOf(scene::motionName(intent.motion));
  if (!clipId) {
    return;
  }
  if (actor.motion != intent.motion) {
    actor.motion = intent.motion;
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

void FieldScene::submit(const Actor& actor, float alpha, glm::vec3 scale) {
  const scene::Pose pose =
      scene::interpolate(world_.registry().get<scene::Transform>(actor.entity), alpha);
  const glm::mat4 world = glm::scale(glm::rotate(glm::translate(glm::mat4{1.0F}, pose.position),
                                                 pose.yaw, glm::vec3{0.0F, 1.0F, 0.0F}),
                                     scale);
  models_->submit({.model = actor.handle, .world = world, .joints = actor.player->jointMatrices()});
}

void FieldScene::update(double frameSeconds, float alpha, bool paused) {
  const double animationSeconds = paused ? 0.0 : frameSeconds;
  animate(*playerActor_, animationSeconds);
  if (partnerClip_) {
    // A scene's clip runs while the field waits for the scene.
    animatePartnerClip(frameSeconds);
  } else {
    animate(*partnerActor_, animationSeconds);
  }
  models_->clear();
  if (!stage_.sceneryHidden) {
    submit(*playerActor_, alpha);
    drawNpcs(animationSeconds, alpha);
    submitMapObjects();
  }
  if (!stage_.partnerHidden) {
    submit(*partnerActor_, alpha, stage_.partnerScale);
  }
  const scene::Pose player =
      scene::interpolate(world_.registry().get<scene::Transform>(playerActor_->entity), alpha);
  // tamer.c: checkMapInteraction calls unsetCameraFollowPlayer when a warp fires, and map.c:
  // tickCameraFollowPlayer only follows while the tamer's state is 0: the camera stays put while
  // the player walks out.
  if (const std::optional<glm::vec3> held = field_->scriptCamera()) {
    cameraFocus_ = *held;
  } else if (!field_->mapChangePending() || !cameraFocus_) {
    cameraFocus_ = player.position;
  }
  // The sprite animations wait while paused. A time-of-day fade under way does not: map.c:
  // tickDaytimeTransition only waits for GAME_STATE, which the field's pause menu leaves alone; the
  // lights switch when it ends.
  mapRenderer_->update(animationSeconds, frameSeconds, *cameraFocus_);
  if (lightingDelay_ > 0.0) {
    lightingDelay_ -= frameSeconds;
    if (lightingDelay_ <= 0.0) {
      applyLighting();
    }
  }
  if (field_->hasMoveTarget() && !field_->path().empty()) {
    render::MapMarker marker = TargetMarker;
    marker.feet = field_->path().back();
    const std::array markers{marker};
    mapRenderer_->setMarkers(markers);
  } else {
    mapRenderer_->setMarkers({});
  }
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

} // namespace dwe::app
