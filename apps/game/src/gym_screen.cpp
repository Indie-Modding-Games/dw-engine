#include "gym_screen.hpp"

#include <dwe/core/log.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace dwe::app {
namespace {

using game::gym::Award;
using game::gym::CourseRules;
using game::gym::GymStats;
using game::gym::ObjectGroup;
using game::gym::ObjectMove;
using game::gym::Point;

constexpr std::string_view GymCourseUnknown = "game: gym: no course {}";
constexpr std::string_view GymChoiceUnknown = "game: gym: no course for choice {}";
constexpr std::string_view GymTrainFailed = "game: gym: training failed: {}";

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

// The course's ports on the game. The partner's walk, animations and camera wait for the field's
// scripted movement (sprint lane S4b): they log and the walk arrives at once.
class GymScreen::Ports final : public game::gym::GymPorts {
public:
  Ports(const GymScreenDesc& desc, const game::gym::GymRules& rules)
      : desc_(&desc), rules_(&rules),
        random_(static_cast<std::uint_fast32_t>(desc.session->partner().randomState())) {}

  void setTraining(bool training) override {
    core::logDebug("[port:gym] tamerSetState({}) partnerSetState({}): pending S4b",
                   training ? 8 : 0, training ? 10 : 1);
  }
  void partnerAnimation(std::int32_t animation) override {
    core::logDebug("[port:gym] startAnimation(partner, {}): pending S4b", animation);
  }
  void holdPartnerAnimation(bool /*hold*/) override {}
  void facePartner(std::int32_t angle) override {
    core::logDebug("[port:gym] partner rotation {}: pending S4b", angle);
  }
  bool walkPartner(std::optional<Point> spot) override {
    if (spot) {
      core::logDebug("[port:gym] tickEntityWalkTo({}, {}): pending S4b", spot->x, spot->z);
    }
    return true;
  }
  void moveCamera(std::optional<Point> spot, std::int32_t speed) override {
    core::logDebug("[port:gym] createCameraMovement({}, speed {}): pending S4b",
                   spot ? std::format("{}, {}", spot->x, spot->z) : std::string{"tamer"}, speed);
  }
  void playSound(std::int32_t slot, std::int32_t sound) override {
    if (game::story::SoundPort* port = desc_->session->sound(); port != nullptr) {
      port->playSound(slot, sound);
    }
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
  void addCloud(Point /*at*/) override { ++clouds_; }
  void removeClouds() override {
    core::logDebug("[port:gym] removeAllCloudFX: {} clouds, no cloud effect yet", clouds_);
    clouds_ = 0;
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
    core::logDebug("[port:gym] TRN_awardHpTrainingGains (trn_reward.c:153): upgraded {}, "
                   "manual {}, slot {}",
                   upgraded, manual, slotTenths);
    return award;
  }

private:
  const GymScreenDesc* desc_;
  const game::gym::GymRules* rules_;
  std::vector<glm::vec2> stored_;
  std::int32_t clouds_ = 0;
  // Only the clouds' scatter is random; seeded from the partner's numbers, a replayed game scatters
  // them the same.
  std::minstd_rand random_;
};

GymScreen::GymScreen(const GymScreenDesc& desc)
    : desc_(desc), ports_(std::make_unique<Ports>(desc_, rules_)) {}

GymScreen::~GymScreen() {
  if (boxShown_) {
    desc_.ui->hide(GymStatsDocument);
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
  // TRN_setup*Training: off the gym's map the course never starts, and the story waits on.
  if (desc_.scene->field().mapId() != rules_.map) {
    core::logDebug("[port:gym] TRN_setupHpTraining (trn_hp.c:58): not on {}, no course",
                   rules_.map);
    return {};
  }
  run_ = std::make_unique<game::gym::CourseRun>(rules_, *found);
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
             {.confirm = intent.confirm || intent.cancelTarget, .cancel = intent.cancelTarget});
  if (run_->finished()) {
    run_.reset();
    complete_ = true;
  }
}

void GymScreen::update() {
  ui::Ui& ui = *desc_.ui;
  const std::optional<game::gym::StatCountup>* countup =
      run_ != nullptr ? &run_->countup() : nullptr;
  if (countup == nullptr || !countup->has_value()) {
    if (boxShown_) {
      ui.hide(GymStatsDocument);
      boxShown_ = false;
      published_.reset();
    }
    return;
  }
  const GymStats& shown = (*countup)->shown();
  if (published_ != shown) {
    screens::ScreenSettings settings;
    for (std::size_t stat = 0; stat < shown.size(); ++stat) {
      settings.push_back({.name = screens::listEntryName("gym.stat", stat, "label"),
                          .value = screens::mainLabelKey(screens::StatLabelsBlock, stat)});
      settings.push_back({.name = screens::listEntryName("gym.stat", stat, "value"),
                          .value = static_cast<double>(shown.at(stat))});
    }
    screens::publish(ui, settings, "gym.revision");
    published_ = shown;
  }
  if (!boxShown_) {
    ui.show(GymStatsDocument, false);
    boxShown_ = true;
  }
}

} // namespace dwe::app
