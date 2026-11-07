#include "gym_screen.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/gym/brain_learning.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/render/map_camera.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/gym_field_ports.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
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

std::size_t objectOf(const ObjectGroup& group, std::int32_t index) {
  return static_cast<std::size_t>(group.first) + static_cast<std::size_t>(index);
}

} // namespace

// The course's ports on the game: the field's and the session's (session::GymFieldPorts), and what
// the screen draws: the map's objects, the effects and the rows of the partner's head.
class GymScreen::Ports final : public session::GymFieldPorts {
public:
  Ports(const GymScreenDesc& desc, const game::gym::GymRules& rules,
        std::vector<GymNotice>& notices)
      : GymFieldPorts({.session = desc.session,
                       .field = &desc.scene->field(),
                       .rules = &rules,
                       .story = desc.story,
                       .inventory = desc.inventory}),
        desc_(&desc), notices_(&notices) {}

  bool animationEnded(Actor actor) override {
    const std::optional<scene::Entity> entity = anyEntityOf(actor);
    return !entity || scene().clipPlayedThrough(*entity);
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
    // overworld.c: getDrawPosition of the partner's location raised by its species' height: the
    // background row the map's objects are placed in.
    game::field::Field& field = desc_->scene->field();
    const render::MapCamera* camera = desc_->scene->camera();
    const assets::DigimonRow* row =
        desc_->species != nullptr ? desc_->species->find(partnerSpecies()) : nullptr;
    if (camera == nullptr || row == nullptr) {
      return std::nullopt;
    }
    glm::vec3 head = field.partner().position;
    head.y += static_cast<float>(row->height);
    const std::optional<render::BackgroundPoint> point = camera->projectToBackground(head);
    if (!point) {
      return std::nullopt;
    }
    core::logDebug("[port:gym] getDrawPosition(partner head): row {:.1f}", point->pixel.y);
    return static_cast<std::int32_t>(point->pixel.y);
  }
  // efe.c: createCloudFX, createParticleFX(0, 0, point, NULL, 0) and removeAllCloudFX, from the
  // training's routine, one of the original's objects.
  void addCloud(Point at) override {
    desc_->scene->fieldEffects().addCloud(at.x, at.z, FieldEffects::Birth::InObjects);
  }
  void addParticle(Point3 at) override {
    // A point without a height is at the partner's (its location.vy).
    const std::int32_t y =
        at.atPartnerHeight ? -static_cast<std::int32_t>(desc_->scene->field().partner().position.y)
                           : at.y;
    desc_->scene->fieldEffects().addSpark(0, 0, {at.x, y, at.z}, 0, FieldEffects::Birth::InObjects);
  }
  void removeClouds() override { desc_->scene->fieldEffects().removeClouds(); }

protected:
  [[nodiscard]] float clipGroundSpeed(scene::Entity entity, std::uint32_t clip) const override {
    return desc_->scene->clipGroundSpeed(entity, clip);
  }
  void awarded(const CourseRules& course, const Award& award) override {
    if (const auto* brain = std::get_if<game::gym::BrainCourse>(&course.detail)) {
      teach(brain->learning, award);
    }
  }

private:
  [[nodiscard]] FieldScene& scene() const { return *desc_->scene; }

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
  std::vector<GymNotice>* notices_;
  std::vector<glm::vec2> stored_;
};

GymScreen::GymScreen(const GymScreenDesc& desc)
    : desc_(desc), ports_(std::make_unique<Ports>(desc_, rules_, notices_)) {}

GymScreen::~GymScreen() {
  ports_->stopHeldSounds();
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
  auto started = start(course->id);
  if (started) {
    batch_.begin(course->id, map);
  }
  return started;
}

void GymScreen::continueBatch(GymBatchMoment moment) {
  if (run_ != nullptr) {
    return;
  }
  moment.map = desc_.scene->field().mapId();
  moment.fieldIdle = moment.fieldIdle && statsBox_.closed() && slotsBox_.closed();
  const auto course = batch_.next(moment);
  if (!course) {
    return;
  }
  core::logInfo("gym: next session of the batch on {}, {} more after it", *course, batch_.left());
  if (auto started = start(*course); !started) {
    core::logWarning("{}", started.error().message);
    batch_.cancel();
  }
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
  recordPublished_ = false;
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
    // A cancel before the award ends the session early, and the batch with it; one that skips the
    // stats' count does not.
    if (pressed && !run_->record().award.has_value()) {
      batch_.cancel();
    }
    run_->tick(*ports_, seconds, 1.0 / framesPerSecond,
               {.confirm = intent.confirm,
                .cancel = pressed,
                .cancelHeld = pressed || std::exchange(cancelHeld_, false)});
    if (run_->record().award.has_value() && !std::exchange(recordPublished_, true)) {
      publishRecord(*run_);
    }
  }
  tickBoxes(seconds);
  if (run_ != nullptr && run_->finished()) {
    run_.reset();
    complete_ = true;
  }
}

void GymScreen::publishRecord(const game::gym::CourseRun& run) {
  // docs/guides/comprobar-entrenamiento.md: a route reads them with expect_range.
  constexpr std::array<std::string_view, 6> StatNames{"hp",      "mp",    "offense",
                                                      "defense", "speed", "brain"};
  const game::gym::SessionRecord& record = run.record();
  ++sessions_;
  screens::ScreenSettings settings;
  settings.push_back({.name = "gym.session.count", .value = static_cast<double>(sessions_)});
  settings.push_back({.name = "gym.session.course", .value = run.course().id});
  settings.push_back({.name = "gym.session.frames", .value = static_cast<double>(record.frames)});
  settings.push_back(
      {.name = "gym.session.max_offset", .value = static_cast<double>(record.maxOffset)});
  settings.push_back({.name = "gym.session.path", .value = static_cast<double>(record.path)});
  for (std::size_t stat = 0; stat < StatNames.size(); ++stat) {
    const std::int32_t gain = record.award ? record.award->gains.at(stat) : 0;
    settings.push_back({.name = std::format("gym.session.gain.{}", StatNames.at(stat)),
                        .value = static_cast<double>(gain)});
  }
  screens::publish(*desc_.ui, settings, "gym.session.revision");
  const GymStats gains = record.award ? record.award->gains : GymStats{};
  core::logInfo("gym: session {} on {}: {} frames, partner {} from its spot, {} covered, gains "
                "{} {} {} {} {} {}",
                run.course().id, desc_.scene->field().mapId(), record.frames, record.maxOffset,
                record.path, gains[0], gains[1], gains[2], gains[3], gains[4], gains[5]);
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
      desc_.scene->worldToWindowFraction(session::gymWorldOf(*run_->course().machine, 0.0F));
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
