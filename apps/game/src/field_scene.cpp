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

// anim.c: startAnimation switches clips at once, for the player, the partner and the NPCs alike;
// the partner ends a clip's pass before its walk changes (Field::movePartner), which is what keeps
// it from popping.
constexpr float ClipFadeSeconds = 0.0F;

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
  // map.c and partner.c size the partner by its species' radius in the species table.
  if (speciesRadius_) {
    field_->setPartnerRadius(speciesRadius_(species));
  }
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

core::Result<void> FieldScene::enterOriginalMap(std::int32_t index, std::int32_t exit) {
  const assets::MapEntry* entry = mapTable_ ? mapTable_->findByOriginalIndex(index) : nullptr;
  if (entry == nullptr) {
    return core::fail(std::format("no map of the original with index {}", index));
  }
  return enterMap(entry->id, exit);
}

void FieldScene::playPartnerClip(std::optional<std::uint32_t> clipId) {
  partnerClip_ = clipId;
  partnerClipStarted_ = false;
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
                      const std::optional<assets::AssetId>& otherModel, MissingClips missingClips) {
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

void FieldScene::applyLighting() {
  render::ModelLighting lighting = mapLighting_;
  const glm::vec3 scale = render::timeOfDayLightScale(time_, fixedTime_, schedule_);
  for (render::DirectionalLight& light : lighting.lights) {
    light.color *= scale;
  }
  models_->setLighting(lighting);
}

void FieldScene::animate(Actor& actor, double seconds) {
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
  const auto& intent = world_.registry().get<scene::AnimationIntent>(actor.entity);
  const bool partner = &actor == partnerActor_.get();
  const std::string_view animation =
      partner ? game::partner::nameOf(partnerAnimation(actor, intent.motion, seconds))
              : scene::motionName(intent.motion);
  std::optional<std::uint32_t> clipId = actor.character.clipOf(animation);
  if (!clipId) {
    clipId = actor.character.clipOf(scene::motionName(intent.motion));
  }
  if (!clipId) {
    if (partner) {
      // Nothing to wait for: the partner's walk changes at once.
      field_->notePartnerPassEnded();
    }
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
  const std::uint64_t passes = actor.player->passes();
  // The field moves the entity with its own speeds; the clip's root motion is left out.
  static_cast<void>(actor.player->advance(static_cast<float>(seconds)));
  // A clip that does not repeat ends its pass on every frame once it holds its last pose.
  if (partner && (actor.player->passes() != passes || actor.player->finished())) {
    field_->notePartnerPassEnded();
  }
}

void FieldScene::setPartnerMood(const game::partner::Conditions& conditions,
                                std::int32_t happiness) {
  partnerConditions_ = conditions;
  partnerHappiness_ = happiness;
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

void FieldScene::submit(const Actor& actor, float alpha, glm::vec3 scale) {
  const scene::Pose pose = field_->drawnPose(actor.entity, alpha);
  const glm::mat4 world = glm::scale(glm::rotate(glm::translate(glm::mat4{1.0F}, pose.position),
                                                 pose.yaw, glm::vec3{0.0F, 1.0F, 0.0F}),
                                     scale);
  models_->submit({.model = actor.handle, .world = world, .joints = actor.player->jointMatrices()});
}

void FieldScene::update(double frameSeconds, float alpha, bool paused) {
  const double animationSeconds = paused ? 0.0 : frameSeconds;
  fighterSeconds_ = std::exchange(fighterSecondsDue_, 0.0);
  animate(*playerActor_, playerSceneClip_ ? frameSeconds : animationSeconds);
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
    submitProps();
  }
  submitEffectModels();
  if (!stage_.partnerHidden) {
    submit(*partnerActor_, alpha, stage_.partnerScale);
  }
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
  mapRenderer_->update(animationSeconds, frameSeconds, *cameraFocus_);
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
  const Actor& actor = *playerActor_;
  if (!actor.model->skin) {
    return std::nullopt;
  }
  const auto joints = actor.player->jointMatrices();
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

void FieldScene::setPlayerYaw(float yaw) {
  auto& transform = world_.registry().get<scene::Transform>(playerActor_->entity);
  transform.yaw = yaw;
  transform.previousYaw = yaw;
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

} // namespace dwe::app
