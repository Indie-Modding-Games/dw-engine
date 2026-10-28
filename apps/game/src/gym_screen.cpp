#include "gym_screen.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/gym/brain_learning.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
#include <random>
#include <string>
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

constexpr std::string_view GymCourseUnknown = "game: gym: no course {}";
constexpr std::string_view GymChoiceUnknown = "game: gym: no course for choice {}";
constexpr std::string_view GymTrainFailed = "game: gym: training failed: {}";
constexpr std::string_view GymOffMap = "game: gym: the courses run on {} only";
constexpr std::string_view NewOrderText = "gym.new_order";
constexpr std::string_view MoveLearnedText = "gym.move_learned";
constexpr std::int32_t Hundred = 100;
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
    clips_.at(indexOf(actor)) = static_cast<std::uint32_t>(animation);
    desc_->scene->field().animateByScript(entityOf(actor), static_cast<std::uint32_t>(animation));
  }
  void holdAnimation(Actor actor, bool hold) override {
    desc_->scene->field().freezeByScript(entityOf(actor), hold);
  }
  void face(Actor actor, std::int32_t angle) override {
    desc_->scene->field().turnByScript(entityOf(actor), assets::yawFromOriginal(angle));
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
  void placePartnerX(std::int32_t x) override {
    game::field::Field& field = desc_->scene->field();
    glm::vec3 position = field.partner().position;
    position.x = static_cast<float>(x);
    field.placeByScript(field.partnerEntity(), position);
  }
  std::string partnerSpecies() override { return desc_->session->partner().state().species; }
  void addCloud(Point /*at*/) override { ++effects_; }
  void addParticle(Point3 /*at*/) override { ++effects_; }
  void removeClouds() override {
    core::logDebug("[port:gym] removeAllCloudFX: {} effects, none drawn yet (phase 5)", effects_);
    effects_ = 0;
  }
  std::int32_t randomBelow(std::int32_t limit) override {
    return std::uniform_int_distribution<std::int32_t>{0, limit - 1}(random_);
  }

  Award award(const CourseRules& course, std::int32_t slotTenths) override {
    session::GameSession& session = *desc_->session;
    Award award{.before = statsOf(session.partner().state().stats.base), .gains = {}};
    const game::partner::TrainingRules& training = session.partnerRules().training;
    bool upgraded = false;
    if (const auto* place = training.findPlace(rules_->place); place != nullptr) {
      if (const auto* trained = place->findCourse(course.id); trained != nullptr) {
        const auto flag = training.upgradeFlags[training.multiplierStat(*trained)];
        const auto set = desc_->story->state().trigger(flag);
        upgraded = set && *set;
      }
    }
    bool manual = false;
    if (const auto item = desc_->inventory->items().handleOf(rules_->manualItem)) {
      manual = desc_->inventory->inventory().countOf(*item) > 0;
    }
    auto trained = session.train({.place = rules_->place,
                                  .course = course.id,
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

  // trn_reward.c: TRN_awardBrainsTrainingGains: new orders, or else maybe a move.
  void teach(const game::gym::BrainLearning& learning, const Award& award) {
    const auto brains = static_cast<std::size_t>(game::partner::Stat::Brain);
    const game::gym::BrainMilestones milestones =
        game::gym::brainMilestones(learning, award.before.at(brains), award.gains.at(brains));
    for (const std::int32_t threshold : milestones.orders) {
      for (const std::string& label : newOrderLabels(threshold)) {
        notices_->push_back({.text = std::string{NewOrderText}, .name = label});
      }
    }
    if (milestones.mayLearnMove) {
      learnMove(learning);
    }
  }

  // The orders the battle's menu gains at this brains (battle.yaml: commands.menus).
  [[nodiscard]] std::vector<std::string> newOrderLabels(std::int32_t threshold) const {
    const auto& menus = desc_->battle->rules().menus;
    const auto tier = std::ranges::find(menus, threshold, &game::battle::MenuTier::brain);
    if (tier == menus.end()) {
      return {};
    }
    std::vector<std::string> labels;
    for (const game::battle::MenuEntry entry : tier->entries) {
      const bool before =
          tier != menus.begin() && std::ranges::contains(std::prev(tier)->entries, entry);
      if (before) {
        continue;
      }
      if (const auto command = commandOf(entry)) {
        labels.push_back(screens::commandLabelKey(*command));
      }
    }
    return labels;
  }

  static std::optional<game::battle::Command> commandOf(game::battle::MenuEntry entry) {
    using game::battle::Command;
    using game::battle::MenuEntry;
    switch (entry) {
    case MenuEntry::Flee:
      return Command::Flee;
    case MenuEntry::YourCall:
      return Command::YourCall;
    case MenuEntry::Attack:
      return Command::Attack;
    case MenuEntry::Moderate:
      return Command::Moderate;
    case MenuEntry::Distance:
      return Command::Distance;
    case MenuEntry::Defend:
      return Command::Defend;
    case MenuEntry::Change:
      return Command::Change;
    case MenuEntry::Finisher:
    case MenuEntry::Techniques:
      return std::nullopt;
    }
    return std::nullopt;
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
    for (const std::string& learned : desc_->battle->learnMove(*id)) {
      notices_->push_back({.text = std::string{MoveLearnedText}, .name = names.move(learned)});
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
  const CourseRules* course = rules->courseOfChoice(*choice);
  if (course == nullptr) {
    return core::fail(std::vformat(GymChoiceUnknown, std::make_format_args(*choice)));
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
  const CourseRules* found = rules_.findCourse(course);
  if (found == nullptr) {
    return core::fail(std::vformat(GymCourseUnknown, std::make_format_args(course)));
  }
  complete_ = false;
  // TRN_setup*Training: off the gym's map no course starts. The original also runs courses on
  // the two other training maps (0x4e, 0x77), not ported yet: rather than leave the story waiting
  // forever there, the course fails and the story goes on.
  if (desc_.scene->field().mapId() != rules_.map) {
    return core::fail(std::vformat(GymOffMap, std::make_format_args(rules_.map)));
  }
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

void GymScreen::tick(double seconds, const game::field::FieldInput& intent) {
  if (run_ == nullptr) {
    return;
  }
  const double framesPerSecond = desc_.session->clock().rules().framesPerSecond;
  run_->tick(*ports_, seconds, 1.0 / framesPerSecond,
             {.confirm = intent.confirm, .cancel = intent.cancelTarget});
  if (run_->finished()) {
    run_.reset();
    complete_ = true;
  }
}

void GymScreen::updateSlots() {
  ui::Ui& ui = *desc_.ui;
  const game::gym::SlotMachine* slots =
      run_ != nullptr && run_->slots().has_value() ? &*run_->slots() : nullptr;
  if (slots == nullptr) {
    if (slotsShown_) {
      ui.hide(GymSlotsDocument);
      slotsShown_ = false;
    }
    return;
  }
  // trn_slots.c: TRN_renderSlotMachine draws each reel from two cells above its line.
  constexpr std::int32_t FirstRow = -2;
  constexpr std::int32_t Rows = 4;
  screens::ScreenSettings settings;
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
  screens::publish(ui, settings, "gym.slots.revision");
  if (!slotsShown_) {
    ui.show(GymSlotsDocument, false);
    slotsShown_ = true;
  }
}

void GymScreen::update() {
  updateSlots();
  ui::Ui& ui = *desc_.ui;
  const std::optional<game::gym::StatCountup>* countup =
      run_ != nullptr ? &run_->countup() : nullptr;
  if (countup == nullptr || !countup->has_value()) {
    if (boxShown_) {
      ui.hide(GymStatsDocument);
      boxShown_ = false;
      published_.reset();
      publishedNotices_ = 0;
    }
    return;
  }
  const GymStats& shown = (*countup)->shown();
  if (published_ != shown || publishedNotices_ != notices_.size()) {
    screens::ScreenSettings settings;
    // The brains course's notes under the stats (trn_reward.c: TRN_renderNewOrdersBox,
    // TRN_renderMoveLearnedBox).
    settings.push_back({.name = "gym.notice.count", .value = static_cast<double>(notices_.size())});
    for (std::size_t index = 0; index < notices_.size(); ++index) {
      settings.push_back({.name = screens::listEntryName("gym.notice", index, "text"),
                          .value = notices_.at(index).text});
      settings.push_back({.name = screens::listEntryName("gym.notice", index, "name"),
                          .value = notices_.at(index).name});
    }
    for (std::size_t stat = 0; stat < shown.size(); ++stat) {
      settings.push_back({.name = screens::listEntryName("gym.stat", stat, "label"),
                          .value = screens::mainLabelKey(screens::StatLabelsBlock, stat)});
      settings.push_back({.name = screens::listEntryName("gym.stat", stat, "value"),
                          .value = static_cast<double>(shown.at(stat))});
    }
    screens::publish(ui, settings, "gym.revision");
    published_ = shown;
    publishedNotices_ = notices_.size();
  }
  if (!boxShown_) {
    ui.show(GymStatsDocument, false);
    boxShown_ = true;
  }
}

} // namespace dwe::app
