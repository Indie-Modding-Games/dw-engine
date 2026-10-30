#include "gym_screen.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/gym/brain_learning.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace dwe::app {
namespace {

using game::gym::Actor;
using game::gym::Award;
using game::gym::CourseRules;
using game::gym::GymStats;
using game::gym::ObjectGroup;
using game::gym::ObjectMove;
using game::gym::Point;
using game::gym::Point3;

constexpr std::string_view GymCourseUnknown = "game: gym: no course {} on {}";
constexpr std::string_view GymChoiceUnknown = "game: gym: no course for choice {} on {}";
constexpr std::string_view GymTrainFailed = "game: gym: training failed: {}";
// trn_reward.c: the brains course's words (reward_messages, reward_mastered): each order
// threshold's lines, in the order of the rules' order_thresholds, then "New orders!" and "was
// mastered!".
constexpr std::array<std::array<std::string_view, 2>, 5> OrderTexts{{
    {"trn.reward_messages.0000", ""},
    {"trn.reward_messages.0001", ""},
    {"trn.reward_messages.0002", "trn.reward_messages.0003"},
    {"trn.reward_messages.0004", ""},
    {"trn.reward_messages.0005", ""},
}};
constexpr std::string_view NewOrdersText = "trn.reward_messages.0006";
constexpr std::string_view MasteredText = "trn.reward_mastered.0000";
// TRN_renderNewOrdersBox draws the text area's rows v 120, 132 and 148 from y 24 in a box at y 18:
// its rows are 6, 18 and 34 pixels down the box.
constexpr std::int32_t NoteFirstRow = 6;
constexpr std::int32_t NoteSecondRow = 18;
constexpr std::int32_t NoteThirdRow = 34;
constexpr std::int32_t Hundred = 100;
// trn_hud.c: TRN_createPostTrainingStatsBox: the box, and the 10-pixel square round the partner's
// node 1 it grows from (getEntityScreenPos(ENTITY_TABLE[1], 1)).
constexpr game::gym::BoxRect StatsBoxRect{.x = -94, .y = -78, .w = 188, .h = 96};
constexpr std::int32_t GrowFromHalf = 5;
// trn_slots.c: TRN_createSlotMachineBox: the box (SLOTS_UI_START), grown from a 16-pixel square
// 8 left and 6 up of the machine (0xa8 - DRAWING_OFFSET_X, 0x7e - DRAWING_OFFSET_Y).
constexpr game::gym::BoxRect SlotsBoxRect{.x = -82, .y = -87, .w = 164, .h = 90};
constexpr std::int32_t MachineLeft = 8;
constexpr std::int32_t MachineUp = 6;
constexpr std::int32_t MachineSquare = 16;
constexpr std::uint32_t PartnerNode = 1;
// ui.c: createAnimatedUIBox and renderUIBoxAnim: the system sounds 0 and 1 of slot 0.
constexpr std::int32_t BoxSoundSlot = 0;
constexpr std::int32_t BoxOpenSound = 0;
constexpr std::int32_t BoxCloseSound = 1;
// The original's screen is 240 lines tall; getEntityScreenPos counts from its middle.
constexpr double ScreenLines = 240.0;
constexpr double Middle = 0.5;
// How far from where the camera looks the gym's view reaches, in world units.
constexpr float OffScreenDistance = 1800.0F;

// The gym's points are in the original's axes (gym.yaml), whose z runs the other way to the
// world's: a course spot taken as a world point sent the partner and the camera off the map.
glm::vec3 worldOf(Point spot, float height) {
  glm::vec3 world = assets::toWorldPosition({spot.x, 0, spot.z});
  world.y = height;
  return world;
}

// A world point in the original's axes, as the courses read the partner's position.
Point originalOf(glm::vec3 world) {
  return Point{.x = static_cast<std::int32_t>(world.x), .z = static_cast<std::int32_t>(-world.z)};
}

std::size_t objectOf(const ObjectGroup& group, std::int32_t index) {
  return static_cast<std::size_t>(group.first) + static_cast<std::size_t>(index);
}

GymStats statsOf(const game::partner::StatBlock& block) {
  GymStats stats{};
  for (std::size_t stat = 0; stat < stats.size(); ++stat) {
    stats.at(stat) = block.values.at(stat);
  }
  return stats;
}

} // namespace

// The course's ports on the game. The story holds the field while a course runs (it waits for the
// gym), so the actors move, turn and animate as a script moves them, and the camera pans as a
// script pans it. The clouds and particles wait for the effects (phase 5).
class GymScreen::Ports final : public game::gym::GymPorts {
public:
  Ports(const GymScreenDesc& desc, const game::gym::GymRules& rules,
        std::vector<GymNotice>& notices)
      : desc_(&desc), rules_(&rules), notices_(&notices),
        random_(static_cast<std::uint_fast32_t>(desc.session->partner().randomState())) {}

  void setTraining(bool training) override {
    // The player's state 8 and partner.c: partnerSetState(10): both stand for the course. A
    // course the console starts finds the field free and holds it itself.
    game::field::Field& field = desc_->scene->field();
    if (training) {
      ownHold_ = !field.heldByScript();
      field.holdForScript(true);
      return;
    }
    for (const Actor actor : std::array{Actor::Tamer, Actor::Partner}) {
      field.driveByScript(entityOf(actor), {});
      field.releaseClipByScript(entityOf(actor));
    }
    walks_ = {};
    clips_ = {};
    if (std::exchange(ownHold_, false)) {
      field.holdForScript(false);
    }
  }
  void animate(Actor actor, std::int32_t animation) override {
    if (animation < 0) {
      return;
    }
    if (actor == Actor::Teacher) {
      animateTeacher(animation);
      return;
    }
    clips_.at(indexOf(actor)) = static_cast<std::uint32_t>(animation);
    desc_->scene->field().animateByScript(entityOf(actor), static_cast<std::uint32_t>(animation));
  }
  bool animationEnded(Actor actor) override {
    const std::optional<scene::Entity> entity = anyEntityOf(actor);
    return !entity || desc_->scene->clipPlayedThrough(*entity);
  }
  void face(Actor actor, std::int32_t angle) override {
    if (actor != Actor::Teacher) {
      desc_->scene->field().turnByScript(entityOf(actor), assets::yawFromOriginal(angle));
    }
  }
  void lookAt(Actor actor, Actor target) override {
    // map.c: entityLookAtLocation.
    const std::optional<scene::Entity> entity = anyEntityOf(actor);
    const std::optional<glm::vec3> from = anyPositionOf(actor);
    const std::optional<glm::vec3> to = anyPositionOf(target);
    if (!entity || !from || !to) {
      return;
    }
    glm::vec3 toward = *to - *from;
    toward.y = 0.0F;
    if (toward.x != 0.0F || toward.z != 0.0F) {
      desc_->scene->field().turnByScript(*entity, assets::yawOfDirection(toward));
    }
  }
  void faceEachOther() override {
    // map.c: entityLookAtLocation, both ways.
    game::field::Field& field = desc_->scene->field();
    const glm::vec3 toPartner = field.partner().position - field.player().position;
    if (toPartner.x != 0.0F || toPartner.z != 0.0F) {
      field.turnByScript(field.playerEntity(), assets::yawOfDirection(toPartner));
      field.turnByScript(field.partnerEntity(), assets::yawOfDirection(-toPartner));
    }
  }
  bool walk(Actor actor, std::optional<Point> spot) override {
    // The original's tickEntityWalkTo faces the target every frame and walks at the walk clip's
    // speed; it ends on the spot's cell, or, walking back, on touching the player.
    game::field::Field& field = desc_->scene->field();
    const scene::Entity entity = entityOf(actor);
    const glm::vec3 position = positionOf(actor);
    const bool toPlayer = !spot.has_value();
    const glm::vec3 target = toPlayer ? field.player().position : worldOf(*spot, position.y);
    glm::vec3 toward = target - position;
    toward.y = 0.0F;
    const float distance = glm::length(toward);
    const game::field::FieldRules& rules = field.rules();
    const float touch = rules.player.radius + rules.partner.mover.radius;
    const bool there = toPlayer
                           ? distance <= touch
                           : assets::collisionCellAt(position) == assets::collisionCellAt(target);
    if (there) {
      field.driveByScript(entity, {});
      walks_.at(indexOf(actor)) = false;
      return true;
    }
    if (!std::exchange(walks_.at(indexOf(actor)), true)) {
      core::logDebug("[port:gym] tickEntityWalkTo({}, {:.0f}, {:.0f})", nameOf(actor), target.x,
                     target.z);
    }
    const float yaw = assets::yawOfDirection(toward);
    field.turnByScript(entity, yaw);
    field.driveByScript(entity, assets::forwardOfYaw(yaw) * walkSpeed(actor));
    return false;
  }
  void moveCamera(std::optional<Point> spot, std::int32_t speed) override {
    // map.c: tickCameraMovement steps the camera to the target in `speed` frames.
    game::field::Field& field = desc_->scene->field();
    const glm::vec3 focus =
        spot ? worldOf(*spot, field.player().position.y) : field.player().position;
    const double framesPerSecond = desc_->session->clock().rules().framesPerSecond;
    field.panCameraByScript(focus, static_cast<float>(speed / framesPerSecond));
  }
  void playSound(std::int32_t slot, std::int32_t sound) override {
    if (game::story::SoundPort* port = desc_->session->sound(); port != nullptr) {
      port->playSound(slot, sound);
    }
  }
  std::int32_t loopSound(std::int32_t slot, std::int32_t sound) override {
    // The sound port plays a sound once; the loop and its stop come with the audio's voices.
    playSound(slot, sound);
    return ++loops_;
  }
  void stopSound(std::int32_t handle) override {
    core::logDebug("[port:gym] thunkStopSoundMask({}): no looping voices yet", handle);
  }
  void storeObjects(const ObjectGroup& group) override {
    stored_.clear();
    render::MapDepthLayers& layers = desc_->scene->mapLayers();
    for (std::int32_t index = 0; index < group.count; ++index) {
      stored_.push_back(layers.offset(objectOf(group, index)));
    }
  }
  void restoreObjects(const ObjectGroup& group) override {
    render::MapDepthLayers& layers = desc_->scene->mapLayers();
    for (std::size_t index = 0; index < stored_.size(); ++index) {
      layers.setOffset(static_cast<std::size_t>(group.first) + index, stored_.at(index));
    }
    stored_.clear();
  }
  void moveObjects(const ObjectGroup& group, ObjectMove move) override {
    render::MapDepthLayers& layers = desc_->scene->mapLayers();
    const glm::vec2 step{static_cast<float>(move.x), static_cast<float>(move.y)};
    for (std::int32_t index = 0; index < group.count; ++index) {
      const std::size_t instance = objectOf(group, index);
      layers.setOffset(instance, layers.offset(instance) + step);
    }
  }
  void showObjects(const ObjectGroup& group, bool shown) override {
    render::MapDepthLayers& layers = desc_->scene->mapLayers();
    for (std::int32_t index = 0; index < group.count; ++index) {
      layers.setHidden(objectOf(group, index), !shown);
    }
  }
  void restartObjects(const ObjectGroup& group) override {
    render::MapDepthLayers& layers = desc_->scene->mapLayers();
    for (std::int32_t index = 0; index < group.count; ++index) {
      layers.restartAnimation(objectOf(group, index));
    }
  }
  std::optional<std::int32_t> objectRow(std::int32_t object) override {
    const render::MapDepthLayers& layers = desc_->scene->mapLayers();
    const auto instance = static_cast<std::size_t>(object);
    const std::optional<glm::vec2> at = layers.position(instance);
    if (!at) {
      return std::nullopt;
    }
    return static_cast<std::int32_t>(std::lround(at->y + layers.offset(instance).y));
  }
  std::optional<std::int32_t> partnerHeadRow() override {
    core::logDebug("[port:gym] getDrawPosition(partner head): not ported yet");
    return std::nullopt;
  }
  std::optional<Point> partnerPosition() override {
    return originalOf(desc_->scene->field().partner().position);
  }
  void movePartner(Point delta) override {
    game::field::Field& field = desc_->scene->field();
    field.placeByScript(field.partnerEntity(), field.partner().position + worldOf(delta, 0.0F));
  }
  void runOn(Actor actor) override {
    // The original's run clip moves its entity itself; a script's clip here does not, so the actor
    // is carried at the clip's ground speed, through the scenery as in the original.
    game::field::Field& field = desc_->scene->field();
    const float yaw = actor == Actor::Tamer ? field.player().yaw : field.partner().yaw;
    const double framesPerSecond = desc_->session->clock().rules().framesPerSecond;
    const float step = walkSpeed(actor) / static_cast<float>(framesPerSecond);
    field.placeByScript(entityOf(actor), positionOf(actor) + assets::forwardOfYaw(yaw) * step);
  }
  bool partnerOnScreen() override {
    // entity.isOnScreen, without the camera's projection: the partner is off screen once it is
    // farther from where the camera looks than the gym's view reaches.
    const game::field::Field& field = desc_->scene->field();
    glm::vec3 apart =
        field.partner().position - field.scriptCamera().value_or(field.player().position);
    apart.y = 0.0F;
    return glm::length(apart) <= OffScreenDistance;
  }
  void placePartner(game::gym::Axis axis, std::int32_t value) override {
    // In the original's axes: its z is the world's -z.
    game::field::Field& field = desc_->scene->field();
    glm::vec3 position = field.partner().position;
    if (axis == game::gym::Axis::X) {
      position.x = static_cast<float>(value);
    } else {
      position.z = -static_cast<float>(value);
    }
    field.placeByScript(field.partnerEntity(), position);
  }
  std::string partnerSpecies() override { return desc_->session->partner().state().species; }
  void addCloud(Point /*at*/) override { ++effects_; }
  void addParticle(Point3 /*at*/) override { ++effects_; }
  void removeClouds() override {
    core::logDebug("[port:gym] removeAllCloudFX: {} effects, none drawn yet (phase 5)", effects_);
    effects_ = 0;
  }
  void closeTextbox() override { static_cast<void>(desc_->story->dialogue().closeBox(0)); }
  std::int32_t randomBelow(std::int32_t limit) override {
    return std::uniform_int_distribution<std::int32_t>{0, limit - 1}(random_);
  }

  Award award(const CourseRules& course, std::int32_t slotTenths) override {
    session::GameSession& session = *desc_->session;
    Award award{.before = statsOf(session.partner().state().stats.base), .gains = {}};
    const game::partner::TrainingRules& training = session.partnerRules().training;
    bool upgraded = false;
    if (const auto* place = training.findPlace(course.place); place != nullptr) {
      if (const auto* trained = place->findCourse(course.gains); trained != nullptr) {
        const auto flag = training.upgradeFlags[training.multiplierStat(*trained)];
        const auto set = desc_->story->state().trigger(flag);
        upgraded = set && *set;
      }
    }
    bool manual = false;
    if (const auto item = desc_->inventory->items().handleOf(rules_->manualItem)) {
      manual = desc_->inventory->inventory().countOf(*item) > 0;
    }
    auto trained = session.train({.place = course.place,
                                  .course = course.gains,
                                  .countsPractice = course.countsPractice,
                                  .slotTenths = slotTenths,
                                  .gymUpgraded = upgraded,
                                  .hasTrainingManual = manual});
    if (!trained) {
      core::logWarning(
          "{}", std::vformat(GymTrainFailed, std::make_format_args(trained.error().message)));
      return award;
    }
    award.gains = statsOf(trained->gains);
    core::logDebug("[port:gym] TRN_award*TrainingGains (trn_reward.c): course {}, upgraded {}, "
                   "manual {}, slot {}",
                   course.id, upgraded, manual, slotTenths);
    if (const auto* brain = std::get_if<game::gym::BrainCourse>(&course.detail)) {
      teach(brain->learning, award);
    }
    return award;
  }

private:
  static std::string_view nameOf(Actor actor) {
    return actor == Actor::Tamer ? "tamer" : "partner";
  }
  static std::size_t indexOf(Actor actor) { return actor == Actor::Tamer ? 0 : 1; }
  [[nodiscard]] scene::Entity entityOf(Actor actor) const {
    const game::field::Field& field = desc_->scene->field();
    return actor == Actor::Tamer ? field.playerEntity() : field.partnerEntity();
  }
  [[nodiscard]] glm::vec3 positionOf(Actor actor) const {
    const game::field::Field& field = desc_->scene->field();
    return actor == Actor::Tamer ? field.player().position : field.partner().position;
  }
  // NPC_ENTITIES[0]: the script id of the map's first character record.
  [[nodiscard]] std::optional<std::int32_t> teacherId() const {
    const assets::MapNpc* record = desc_->scene->field().npcs().record(0);
    return record != nullptr ? std::optional{record->scriptId} : std::nullopt;
  }
  [[nodiscard]] std::optional<scene::Entity> anyEntityOf(Actor actor) const {
    if (actor != Actor::Teacher) {
      return entityOf(actor);
    }
    const auto id = teacherId();
    return id ? desc_->scene->field().npcs().entityOf(*id) : std::nullopt;
  }
  [[nodiscard]] std::optional<glm::vec3> anyPositionOf(Actor actor) const {
    if (actor != Actor::Teacher) {
      return positionOf(actor);
    }
    const auto id = teacherId();
    return id ? desc_->scene->field().npcs().positionOf(*id) : std::nullopt;
  }
  // overworld.c: setActiveAnim, as the story animates an NPC: 0 hands it back to its own motion.
  void animateTeacher(std::int32_t animation) {
    game::field::Field& field = desc_->scene->field();
    const auto id = teacherId();
    const auto entity = anyEntityOf(Actor::Teacher);
    if (!id || !entity || !field.npcs().setScriptedAnimation(*id, animation)) {
      return;
    }
    if (animation == 0) {
      field.releaseClipByScript(*entity);
    } else {
      field.animateByScript(*entity, static_cast<std::uint32_t>(animation));
    }
  }
  // The last clip's ground speed (anim.c: applyRootMomentum); a clip without one walks at the
  // field's walk speed.
  [[nodiscard]] float walkSpeed(Actor actor) const {
    const game::field::FieldRules& rules = desc_->scene->field().rules();
    const float fallback =
        actor == Actor::Tamer ? rules.player.walkSpeed : rules.partner.mover.walkSpeed;
    const std::optional<std::uint32_t> clip = clips_.at(indexOf(actor));
    const float speed = clip ? desc_->scene->clipGroundSpeed(entityOf(actor), *clip) : 0.0F;
    return speed > 0.0F ? speed : fallback;
  }

  // trn_reward.c: TRN_awardBrainsTrainingGains: new orders, or else maybe a move. Each order
  // threshold crossed draws its words on the box's first row, over the last one's, and 300 adds a
  // second row; "New orders!" follows in the plain color.
  void teach(const game::gym::BrainLearning& learning, const Award& award) {
    const auto brains = static_cast<std::size_t>(game::partner::Stat::Brain);
    const game::gym::BrainMilestones milestones =
        game::gym::brainMilestones(learning, award.before.at(brains), award.gains.at(brains));
    const auto rows = game::gym::orderRows(learning, milestones, [](std::size_t rank) {
      return rank < OrderTexts.size() && !OrderTexts.at(rank).back().empty();
    });
    if (rows && rows->first < OrderTexts.size()) {
      notices_->push_back({.text = std::string{OrderTexts.at(rows->first).front()},
                           .row = NoteFirstRow,
                           .order = true});
      if (rows->second) {
        notices_->push_back({.text = std::string{OrderTexts.at(*rows->second).back()},
                             .row = NoteSecondRow,
                             .order = true});
      }
      notices_->push_back(
          {.text = std::string{NewOrdersText}, .row = rows->second ? NoteThirdRow : NoteSecondRow});
      return;
    }
    if (milestones.mayLearnMove) {
      learnMove(learning);
    }
  }

  // trn_reward.c: TRN_tryLearnMove.
  void learnMove(const game::gym::BrainLearning& learning) {
    session::GameSession& session = *desc_->session;
    const std::string& species = session.partner().state().species;
    const assets::DigimonRow* row =
        desc_->species != nullptr ? desc_->species->find(species) : nullptr;
    if (row == nullptr) {
      return;
    }
    const game::battle::MoveCatalog& catalog = desc_->battle->catalog();
    const auto idOf = [&catalog](std::int32_t number) -> std::optional<std::string> {
      for (std::size_t ref = 0; ref < catalog.size(); ++ref) {
        if (catalog.at(ref).index == number) {
          return catalog.at(ref).id;
        }
      }
      return std::nullopt;
    };
    const auto learnable = [&](std::int32_t number) {
      const auto id = idOf(number);
      return id && std::ranges::contains(row->moves, id) &&
             !session::knows(desc_->battle->moves(), *id);
    };
    const auto move =
        game::gym::trainingMove(learning, row->special, learnable, randomBelow(Hundred));
    if (!move) {
      return;
    }
    const auto id = idOf(*move);
    if (!id) {
      return;
    }
    const screens::NameKeys names{desc_->species, nullptr, &catalog};
    // The move's name on the first row and "was mastered!" on the second
    // (TRN_renderMoveLearnedBox).
    for (const std::string& learned : desc_->battle->learnMove(*id)) {
      notices_->push_back({.text = names.move(learned), .row = NoteFirstRow, .move = true});
      notices_->push_back({.text = std::string{MasteredText}, .row = NoteSecondRow, .move = true});
    }
  }

  const GymScreenDesc* desc_;
  const game::gym::GymRules* rules_;
  std::vector<GymNotice>* notices_;
  std::vector<glm::vec2> stored_;
  std::int32_t effects_ = 0;
  std::int32_t loops_ = 0;
  // Whether the course holds the field itself; each actor's walk and last clip (player, partner).
  bool ownHold_ = false;
  std::array<bool, 2> walks_{};
  std::array<std::optional<std::uint32_t>, 2> clips_{};
  // Only the clouds' scatter and the moves taught are random; seeded from the partner's numbers, a
  // replayed game does the same.
  std::minstd_rand random_;
};

GymScreen::GymScreen(const GymScreenDesc& desc)
    : desc_(desc), ports_(std::make_unique<Ports>(desc_, rules_, notices_)) {}

GymScreen::~GymScreen() {
  if (boxShown_) {
    desc_.ui->hide(GymStatsDocument);
  }
  if (slotsShown_) {
    desc_.ui->hide(GymSlotsDocument);
  }
}

void GymScreen::loadLibrary() {
  core::logDebug("[port:gym] loadTrainingLibrary (map.c:5457)");
  complete_ = false;
}

core::Result<void> GymScreen::startChosen() {
  auto rules = game::gym::readGymRules(desc_.session->rules().current());
  if (!rules) {
    return std::unexpected{rules.error()};
  }
  auto choice = desc_.story->state().variable(rules->courseVariable);
  if (!choice) {
    return std::unexpected{choice.error()};
  }
  const std::string& map = desc_.scene->field().mapId();
  const CourseRules* course = rules->courseOfChoice(map, *choice);
  if (course == nullptr) {
    return core::fail(std::vformat(GymChoiceUnknown, std::make_format_args(*choice, map)));
  }
  core::logDebug("[port:gym] call_routine 7 (script_interp.c:1682): choice {}, course {}", *choice,
                 course->id);
  return start(course->id);
}

core::Result<void> GymScreen::start(std::string_view course) {
  if (run_ != nullptr) {
    return {};
  }
  auto rules = game::gym::readGymRules(desc_.session->rules().current());
  if (!rules) {
    return std::unexpected{rules.error()};
  }
  rules_ = std::move(*rules);
  // TRN_setup*Training and TRN2_setup*Training: each training map has its own courses; off them
  // no course starts. Rather than leave the story waiting forever, the course fails and the story
  // goes on.
  const std::string& map = desc_.scene->field().mapId();
  const CourseRules* found = rules_.findCourse(map, course);
  if (found == nullptr) {
    return core::fail(std::vformat(GymCourseUnknown, std::make_format_args(course, map)));
  }
  complete_ = false;
  // TRN_setup*Training: readPStat(0xf6) == 1 puts the roulette over the course.
  auto slots = desc_.story->state().variable(rules_.slotVariable);
  if (!slots) {
    return std::unexpected{slots.error()};
  }
  notices_.clear();
  run_ = std::make_unique<game::gym::CourseRun>(rules_, *found, *slots == 1);
  return {};
}

bool GymScreen::takeComplete() {
  return std::exchange(complete_, false);
}

void GymScreen::pressCancel(bool pressed, bool held) {
  cancelPressed_ = pressed;
  cancelHeld_ = held;
}

void GymScreen::tick(double seconds, const game::field::FieldInput& intent) {
  if (run_ != nullptr) {
    const double framesPerSecond = desc_.session->clock().rules().framesPerSecond;
    const bool pressed = intent.cancelTarget || std::exchange(cancelPressed_, false);
    run_->tick(*ports_, seconds, 1.0 / framesPerSecond,
               {.confirm = intent.confirm,
                .cancel = pressed,
                .cancelHeld = pressed || std::exchange(cancelHeld_, false)});
  }
  tickBoxes(seconds);
  if (run_ != nullptr && run_->finished()) {
    run_.reset();
    complete_ = true;
  }
}

void GymScreen::tickBoxes(double seconds) {
  // TRN_createSlotMachineBox and TRN_tickSlotMachine (state 6): the roulette grows out of the
  // course's machine and shrinks back into it once the line was seen.
  const game::gym::SlotMachine* slots =
      run_ != nullptr && run_->slots().has_value() ? &*run_->slots() : nullptr;
  if (slots != nullptr && !slots->closing() && slotsBox_.closed()) {
    openBox(slotsBox_, machineOnScreen(), SlotsBoxRect);
  } else if ((slots == nullptr || slots->closing()) && slotsBox_.opened()) {
    slotsBox_.close(run_ != nullptr ? std::optional{machineOnScreen()} : std::nullopt);
    playUiSound(BoxCloseSound);
  }
  // TRN_createPostTrainingStatsBox and TRN_closeUIBox: the stats grow out of the partner and
  // shrink back into it.
  const bool counting = run_ != nullptr && run_->countup().has_value();
  if (counting && statsBox_.closed()) {
    openBox(statsBox_, partnerOnScreen(), StatsBoxRect);
  } else if (!counting && statsBox_.opened()) {
    statsBox_.close();
    playUiSound(BoxCloseSound);
  }
  if (statsBox_.closed() && slotsBox_.closed()) {
    boxSeconds_ = 0.0;
    return;
  }
  const double frameSeconds = 1.0 / desc_.session->clock().rules().framesPerSecond;
  boxSeconds_ += seconds;
  while (boxSeconds_ >= frameSeconds) {
    boxSeconds_ -= frameSeconds;
    statsBox_.tickFrame();
    slotsBox_.tickFrame();
  }
}

void GymScreen::openBox(game::gym::GrowingBox& box, game::gym::BoxRect from,
                        game::gym::BoxRect to) const {
  // ui.c: createAnimatedUIBox sounds, and the frame that creates the box draws its first outline.
  box.open(from, to);
  playUiSound(BoxOpenSound);
  box.tickFrame();
}

game::gym::BoxRect GymScreen::machineOnScreen() const {
  // TRN_createSlotMachineBox: the 16-pixel square at the machine's screen position, 8 left and 6
  // up of it; the screen's middle when it is not in view.
  game::gym::BoxRect square{
      .x = -MachineLeft, .y = -MachineUp, .w = MachineSquare, .h = MachineSquare};
  if (run_ == nullptr || !run_->course().machine) {
    return square;
  }
  const std::optional<glm::vec2> point =
      desc_.scene->worldToWindowFraction(worldOf(*run_->course().machine, 0.0F));
  if (const auto at = pixelsFromMiddle(point)) {
    square.x += at->x;
    square.y += at->z;
  }
  return square;
}

std::optional<game::gym::Point>
GymScreen::pixelsFromMiddle(std::optional<glm::vec2> fraction) const {
  // getEntityScreenPos and worldPosToScreenPos: the original's pixels from the screen's middle.
  const render::MapCamera* camera = desc_.scene->camera();
  if (camera == nullptr || !fraction || camera->viewportSize().y <= 0.0F) {
    return std::nullopt;
  }
  const double aspect =
      static_cast<double>(camera->viewportSize().x) / static_cast<double>(camera->viewportSize().y);
  return game::gym::Point{
      .x = static_cast<std::int32_t>(std::lround((fraction->x - Middle) * ScreenLines * aspect)),
      .z = static_cast<std::int32_t>(std::lround((fraction->y - Middle) * ScreenLines))};
}

game::gym::BoxRect GymScreen::partnerOnScreen() const {
  // getEntityScreenPos(partner, 1): the 10-pixel square round its node; the screen's middle when
  // the partner is not drawn.
  game::gym::BoxRect square{
      .x = -GrowFromHalf, .y = -GrowFromHalf, .w = 2 * GrowFromHalf, .h = 2 * GrowFromHalf};
  if (const auto at =
          pixelsFromMiddle(desc_.scene->fighterNodeOnWindow(std::nullopt, PartnerNode))) {
    square.x += at->x;
    square.y += at->z;
  }
  return square;
}

void GymScreen::playUiSound(std::int32_t sound) const {
  if (game::story::SoundPort* port = desc_.session->sound(); port != nullptr) {
    port->playSound(BoxSoundSlot, sound);
  }
}

void GymScreen::updateSlots() {
  ui::Ui& ui = *desc_.ui;
  if (slotsBox_.closed()) {
    if (slotsShown_) {
      ui.hide(GymSlotsDocument);
      slotsShown_ = false;
    }
    return;
  }
  const game::gym::SlotMachine* slots =
      run_ != nullptr && run_->slots().has_value() ? &*run_->slots() : nullptr;
  const std::optional<game::gym::BoxRect> outline = slotsBox_.outline();
  screens::ScreenSettings settings;
  // ui.c: renderUIBoxAnim draws the outline while the box grows or shrinks; the machine only while
  // it stands.
  const bool standing = slotsBox_.standing() && slots != nullptr;
  settings.push_back(
      {.name = "gym.slots.box.state", .value = outline ? 1.0 : (standing ? 2.0 : 0.0)});
  if (outline) {
    settings.push_back({.name = "gym.slots.box.x", .value = static_cast<double>(outline->x)});
    settings.push_back({.name = "gym.slots.box.y", .value = static_cast<double>(outline->y)});
    settings.push_back({.name = "gym.slots.box.w", .value = static_cast<double>(outline->w)});
    settings.push_back({.name = "gym.slots.box.h", .value = static_cast<double>(outline->h)});
  }
  if (standing) {
    // trn_slots.c: TRN_renderSlotMachine draws each reel from two cells above its line.
    constexpr std::int32_t FirstRow = -2;
    constexpr std::int32_t Rows = 4;
    for (std::size_t reel = 0; reel < game::gym::SlotMachine::Reels; ++reel) {
      for (std::int32_t row = 0; row < Rows; ++row) {
        settings.push_back(
            {.name = screens::listEntryName("gym.slots", reel, std::format("row{}", row)),
             .value = static_cast<double>(slots->symbolAt(reel, FirstRow + row))});
      }
      settings.push_back({.name = screens::listEntryName("gym.slots", reel, "offset"),
                          .value = static_cast<double>(slots->drawnScroll(reel))});
    }
    settings.push_back(
        {.name = "gym.slots.payout", .value = static_cast<double>(slots->payout().value_or(-1))});
  }
  screens::publish(ui, settings, "gym.slots.revision");
  if (!slotsShown_) {
    ui.show(GymSlotsDocument, false);
    slotsShown_ = true;
  }
}

void GymScreen::update() {
  updateSlots();
  ui::Ui& ui = *desc_.ui;
  if (statsBox_.closed()) {
    if (boxShown_) {
      ui.hide(GymStatsDocument);
      boxShown_ = false;
      published_.reset();
      publishedNotices_ = 0;
      publishedOutline_.reset();
      publishedStanding_ = false;
    }
    return;
  }
  const std::optional<game::gym::StatCountup>* countup =
      run_ != nullptr ? &run_->countup() : nullptr;
  const bool counting = countup != nullptr && countup->has_value();
  const std::optional<game::gym::BoxRect> outline = statsBox_.outline();
  const bool standing = statsBox_.standing() && counting;
  const bool changed =
      (counting && (published_ != (*countup)->shown() || publishedNotices_ != notices_.size())) ||
      publishedOutline_ != outline || publishedStanding_ != standing;
  if (changed) {
    screens::ScreenSettings settings;
    // ui.c: renderUIBoxAnim draws the outline while the box grows or shrinks; the box and its
    // contents only while it stands (renderUIBoxStatic).
    const double state = outline ? 1.0 : (standing ? 2.0 : 0.0);
    settings.push_back({.name = "gym.box.state", .value = state});
    if (outline) {
      settings.push_back({.name = "gym.box.x", .value = static_cast<double>(outline->x)});
      settings.push_back({.name = "gym.box.y", .value = static_cast<double>(outline->y)});
      settings.push_back({.name = "gym.box.w", .value = static_cast<double>(outline->w)});
      settings.push_back({.name = "gym.box.h", .value = static_cast<double>(outline->h)});
    }
    if (counting) {
      publishCountup(settings, **countup);
      published_ = (*countup)->shown();
    }
    screens::publish(ui, settings, "gym.revision");
    publishedNotices_ = notices_.size();
    publishedOutline_ = outline;
    publishedStanding_ = standing;
  }
  if (!boxShown_) {
    ui.show(GymStatsDocument, false);
    boxShown_ = true;
  }
}

void GymScreen::publishCountup(screens::ScreenSettings& settings,
                               const game::gym::StatCountup& countup) const {
  // The brains course's box under the stats (trn_reward.c: TRN_renderNewOrdersBox,
  // TRN_renderMoveLearnedBox).
  settings.push_back({.name = "gym.notice.count", .value = static_cast<double>(notices_.size())});
  settings.push_back(
      {.name = "gym.notice.move", .value = !notices_.empty() && notices_.front().move ? 1.0 : 0.0});
  for (std::size_t index = 0; index < notices_.size(); ++index) {
    settings.push_back({.name = screens::listEntryName("gym.notice", index, "text"),
                        .value = notices_.at(index).text});
    settings.push_back({.name = screens::listEntryName("gym.notice", index, "row"),
                        .value = static_cast<double>(notices_.at(index).row)});
    settings.push_back({.name = screens::listEntryName("gym.notice", index, "order"),
                        .value = notices_.at(index).order ? 1.0 : 0.0});
  }
  // trn_hud.c: TRN_renderPostTrainingStatsBox: each stat's value and bar (blue when the session
  // changed it), and the gain left to count.
  const GymStats& shown = countup.shown();
  for (std::size_t stat = 0; stat < shown.size(); ++stat) {
    settings.push_back({.name = screens::listEntryName("gym.stat", stat, "label"),
                        .value = screens::mainLabelKey(screens::StatLabelsBlock, stat)});
    settings.push_back({.name = screens::listEntryName("gym.stat", stat, "value"),
                        .value = static_cast<double>(shown.at(stat))});
    settings.push_back({.name = screens::listEntryName("gym.stat", stat, "gain"),
                        .value = static_cast<double>(countup.left().at(stat))});
    settings.push_back({.name = screens::listEntryName("gym.stat", stat, "changed"),
                        .value = countup.changed().at(stat) ? 1.0 : 0.0});
    settings.push_back({.name = screens::listEntryName("gym.stat", stat, "max"),
                        .value = static_cast<double>(countup.capOf(stat))});
  }
}

} // namespace dwe::app
