#include "fishing_screen.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/screens/name_keys.hpp>

#include <algorithm>
#include <format>
#include <utility>

#include "common.hpp"
#include "field_scene.hpp"
#include "messages.hpp"

namespace dwe::app {

namespace {

using game::fishing::FishingEvent;
using game::fishing::FishingEventKind;
using game::fishing::FishingMessage;

constexpr std::string_view FishCommand = "fish";
constexpr std::string_view ChooseBaitCommand = "choose_bait";
constexpr std::string_view NoBaitCommand = "no_bait";
constexpr std::string_view NoticeAnswer = "fishing_notice_ok";
constexpr std::string_view AvailabilitySetting = "pause_menu.fishing";
constexpr std::string_view BaitsSetting = "fishing_baits";
constexpr std::string_view BaitNamePrefix = "fishing_bait_name.";
constexpr std::string_view BaitCountPrefix = "fishing_bait_count.";
constexpr std::string_view NoticeTextSetting = "fishing_notice.text";
constexpr std::string_view NoticeItemSetting = "fishing_notice.item";
constexpr std::string_view NoticeSizeSetting = "fishing_notice.size";
constexpr std::string_view TensionSetting = "fishing_hud.tension";

constexpr std::string_view CaughtText = "fishing.caught";
constexpr std::string_view CaughtLetGoText = "fishing.caught_let_go";
constexpr std::string_view StuckText = "fishing.stuck";
constexpr std::string_view StuckBagFullText = "fishing.stuck_bag_full";

// The original's frame: the fishing counts its timers in 20ths of a second.
constexpr double FishingFrameSeconds = 1.0 / 20.0;

// FISH_getRodTipPos: the bone of the hand and the rod's grip on it, and where the bobber and the
// bait hang below it, in original units (y down).
constexpr std::uint32_t HandNode = 6;
constexpr glm::vec3 GripOffset{28.0F, 0.0F, 2.0F};
constexpr float BobberDrop = 17.0F;
constexpr float BaitDrop = 93.0F;
// FISH_updateRodPose bends the rod model, which is not drawn yet: its tip is taken this far ahead
// of the hand and this high above it.
constexpr float RodReach = 160.0F;
constexpr float RodRise = 120.0F;
// A hand where the model has no such bone.
constexpr float HandHeight = 100.0F;

constexpr render::MapMarker BobberMarker{.feet = glm::vec3{0.0F},
                                         .width = 14.0F,
                                         .height = 14.0F,
                                         .color = glm::vec4{0.95F, 0.25F, 0.2F, 1.0F}};
constexpr render::MapMarker BaitMarker{.feet = glm::vec3{0.0F},
                                       .width = 10.0F,
                                       .height = 10.0F,
                                       .color = glm::vec4{0.95F, 0.85F, 0.4F, 1.0F}};
constexpr render::MapMarker FishMarker{.feet = glm::vec3{0.0F},
                                       .width = 0.0F,
                                       .height = 6.0F,
                                       .color = glm::vec4{0.05F, 0.1F, 0.2F, 0.0F}};
// A fish's shadow is this wide per centimetre of the fish.
constexpr float FishWidthPerSize = 0.6F;
constexpr float FishMinWidth = 12.0F;
constexpr float FishShadowAlpha = 0.6F;

std::string_view textOf(FishingMessage message) {
  switch (message) {
  case FishingMessage::BaitTaken:
    return "fishing.bait_taken";
  case FishingMessage::TooLate:
    return "fishing.too_late";
  case FishingMessage::TooSoon:
    return "fishing.too_soon";
  case FishingMessage::Hooked:
    return "fishing.hooked";
  case FishingMessage::LineCut:
    return "fishing.line_cut";
  case FishingMessage::HookLost:
    return "fishing.hook_lost";
  case FishingMessage::Snagged:
    return "fishing.snagged";
  }
  return "fishing.too_late";
}

// The map's screen as the fishing sees it (FISH_projectPoint): background pixels from its optical
// centre, which is the middle of the background.
class CameraView final : public game::fishing::FishingView {
public:
  explicit CameraView(const render::MapCamera& camera)
      : camera_{&camera}, centre_{camera.desc().backgroundSize * 0.5F} {}

  [[nodiscard]] std::optional<glm::vec2> toScreen(glm::vec3 world) const override {
    const std::optional<render::BackgroundPoint> point = camera_->projectToBackground(world);
    if (!point) {
      return std::nullopt;
    }
    return point->pixel - centre_;
  }
  [[nodiscard]] std::optional<glm::vec3> toGround(glm::vec2 screen) const override {
    return camera_->pickGround(camera_->backgroundToWindow(screen + centre_));
  }
  [[nodiscard]] glm::vec2 screenMin() const override {
    return glm::max(camera_->scroll(), glm::vec2{0.0F}) - centre_;
  }
  [[nodiscard]] glm::vec2 screenMax() const override {
    return glm::min(camera_->scroll() + camera_->visibleSize(), camera_->desc().backgroundSize) -
           centre_;
  }

private:
  const render::MapCamera* camera_;
  glm::vec2 centre_;
};

} // namespace

FishingScreen::FishingScreen(session::FishingAdapter& fishing,
                             const session::InventoryAdapter& inventory,
                             session::GameSession& session, ui::Ui& ui,
                             game::story::SoundPort* sound)
    : fishing_{&fishing}, inventory_{&inventory}, session_{&session}, ui_{&ui}, sound_{sound} {}

bool FishingScreen::active() const {
  const game::fishing::Fishing* water = fishing_->fishing();
  return (water != nullptr && water->fishing()) || noticeShown_;
}

void FishingScreen::enterMap(FieldScene& scene, std::string_view mapId) {
  fishing_->enterMap(mapId);
  scene.setPlayerClip(std::nullopt);
  scene.setExtraMarkers({});
  notices_.clear();
  if (noticeShown_) {
    ui_->hide(FishingNoticeDocument);
    noticeShown_ = false;
  }
  if (gaugeShown_) {
    ui_->hide(FishingHudDocument);
    gaugeShown_ = false;
  }
  frameSeconds_ = 0.0;
}

void FishingScreen::update(bool fieldIdle) {
  const session::FishingAvailability availability =
      fieldIdle ? fishing_->availability() : session::FishingAvailability::None;
  const auto offer = static_cast<std::int32_t>(availability);
  if (offered_ != offer) {
    offered_ = offer;
    ui_->setSetting(AvailabilitySetting, static_cast<double>(offer));
  }
}

bool FishingScreen::handle(const ui::UiCommand& command) {
  game::fishing::Fishing* water = fishing_->fishing();
  if (command.name == FishCommand) {
    const std::optional<game::fishing::Rod> rod = fishing_->bestRod();
    if (water == nullptr || active() || !rod ||
        fishing_->availability() != session::FishingAvailability::Ready) {
      return true;
    }
    ui_->hide(MenuDocument);
    water->start(*rod);
    core::logDebug("[port:fishing] initializeFishing (game_menu.c:2217): the rod is out");
    return true;
  }
  if (command.name == ChooseBaitCommand || command.name == NoBaitCommand) {
    ui_->hide(FishingBaitDocument);
    std::optional<std::int32_t> bait;
    if (const double* index = std::get_if<double>(&command.value);
        index != nullptr && command.name == ChooseBaitCommand && *index >= 0.0 &&
        static_cast<std::size_t>(*index) < baits_.size()) {
      bait = fishing_->itemNumber(baits_[static_cast<std::size_t>(*index)]);
    }
    if (water != nullptr) {
      water->chooseBait(bait);
    }
    return true;
  }
  if (command.name == NoticeAnswer && noticeShown_) {
    ui_->hide(FishingNoticeDocument);
    noticeShown_ = false;
    showNextNotice();
    return true;
  }
  return false;
}

void FishingScreen::tick(FieldScene& scene, const FishingButtons& buttons, double seconds) {
  // The presses wait for the next fishing frame.
  confirmPressed_ = confirmPressed_ || (buttons.confirmDown && !confirmDown_);
  confirmReleased_ = confirmReleased_ || (!buttons.confirmDown && confirmDown_);
  cancelPressed_ = cancelPressed_ || buttons.cancelPressed;
  confirmDown_ = buttons.confirmDown;
  buttons_ = buttons;
  game::fishing::Fishing* water = fishing_->fishing();
  if (water == nullptr || noticeShown_) {
    return;
  }
  frameSeconds_ += seconds;
  while (frameSeconds_ >= FishingFrameSeconds && !noticeShown_ && water->fishing()) {
    frameSeconds_ -= FishingFrameSeconds;
    frame(scene);
  }
  if (!water->fishing()) {
    frameSeconds_ = 0.0;
  }
  showMarkers(scene);
  showGauge();
}

game::fishing::FishingInput FishingScreen::inputOf(const FieldScene& scene) const {
  const game::field::CharacterState& player = scene.field().player();
  const glm::vec3 facing = assets::forwardOfYaw(player.yaw);
  const glm::vec3 hand = scene.playerNodePoint(HandNode, GripOffset)
                             .value_or(player.position + glm::vec3{0.0F, HandHeight, 0.0F});
  const game::fishing::Fishing& water = *fishing_->fishing();
  return {.confirmPressed = confirmPressed_,
          .confirmHeld = confirmDown_,
          .confirmReleased = confirmReleased_,
          .cancelPressed = cancelPressed_,
          // The map camera follows the player; the free look is not there yet.
          .cameraPressed = false,
          .cameraHeld = false,
          .turnLeft = buttons_.left,
          .turnRight = buttons_.right,
          .animationPlaying = scene.playerClipPlaying(),
          .cameraSettled = true,
          .lineStart = hand + facing * RodReach + glm::vec3{0.0F, RodRise, 0.0F},
          .bobberRest = hand - glm::vec3{0.0F, BobberDrop, 0.0F},
          .baitHold = hand - glm::vec3{0.0F, BaitDrop, 0.0F},
          .player = player.position,
          .playerFacing = facing,
          .hour = session_->clock().time().hour,
          .hasBait = water.choosingBait() || !fishing_->baits().empty()};
}

void FishingScreen::frame(FieldScene& scene) {
  const render::MapCamera* camera = scene.camera();
  if (camera == nullptr) {
    return;
  }
  const CameraView view{*camera};
  const game::fishing::FishingInput input = inputOf(scene);
  confirmPressed_ = false;
  confirmReleased_ = false;
  cancelPressed_ = false;
  const game::fishing::FishingOutput out =
      fishing_->fishing()->tick(input, view, session_->random());
  follow(scene, out);
}

void FishingScreen::follow(FieldScene& scene, const game::fishing::FishingOutput& out) {
  float yaw = scene.field().player().yaw;
  if (out.playerTurn != 0) {
    yaw += assets::yawFromOriginal(out.playerTurn);
  }
  if (out.playerRotation) {
    yaw = assets::yawFromOriginal(*out.playerRotation);
  }
  if (yaw != scene.field().player().yaw) {
    scene.setPlayerYaw(yaw);
  }
  for (const FishingEvent& event : out.events) {
    switch (event.kind) {
    case FishingEventKind::Sound:
      if (sound_ != nullptr) {
        sound_->playSound(fishing_->soundSlot(), event.value);
      }
      break;
    case FishingEventKind::Animation:
      scene.setPlayerClip(static_cast<std::uint32_t>(event.value));
      break;
    case FishingEventKind::Message:
      notices_.push_back({.text = std::string{textOf(static_cast<FishingMessage>(event.value))},
                          .item = {},
                          .size = 0});
      break;
    case FishingEventKind::OpenBaitMenu:
      openBaitMenu();
      break;
    case FishingEventKind::Ended:
      scene.setPlayerClip(std::nullopt);
      break;
    case FishingEventKind::SeaSerpentLanded:
      core::logInfo("{}", messages::SeaSerpentScene);
      break;
    default:
      break;
    }
  }
  const screens::NameKeys names{nullptr, &inventory_->items(), nullptr};
  for (const session::FishingCatch& caught : fishing_->apply(*session_, out)) {
    const bool fish = caught.kind == session::FishingCatch::Kind::Fish;
    const std::string_view text = fish ? (caught.kept ? CaughtText : CaughtLetGoText)
                                       : (caught.kept ? StuckText : StuckBagFullText);
    notices_.push_back({.text = std::string{text},
                        .item = names.item(caught.item),
                        .size = fish ? caught.size : 0});
  }
  showNextNotice();
}

void FishingScreen::openBaitMenu() {
  baits_ = fishing_->baits();
  const screens::NameKeys names{nullptr, &inventory_->items(), nullptr};
  ui_->setSetting(BaitsSetting, static_cast<double>(baits_.size()));
  for (std::size_t index = 0; index < baits_.size(); ++index) {
    ui_->setSetting(std::format("{}{}", BaitNamePrefix, index), names.item(baits_[index]));
    ui_->setSetting(std::format("{}{}", BaitCountPrefix, index),
                    static_cast<double>(inventory_->inventory().countOf(baits_[index])));
  }
  ui_->show(FishingBaitDocument, true);
}

void FishingScreen::showNextNotice() {
  if (noticeShown_ || notices_.empty()) {
    return;
  }
  const Notice notice = std::move(notices_.front());
  notices_.pop_front();
  ui_->setSetting(NoticeItemSetting, notice.item);
  ui_->setSetting(NoticeSizeSetting, static_cast<double>(notice.size));
  ui_->setSetting(NoticeTextSetting, notice.text);
  ui_->show(FishingNoticeDocument, true);
  noticeShown_ = true;
}

void FishingScreen::showMarkers(FieldScene& scene) const {
  const game::fishing::Fishing& water = *fishing_->fishing();
  std::vector<render::MapMarker> markers;
  for (const game::fishing::Fish& fish : water.fish()) {
    if (fish.opacity <= 0) {
      continue;
    }
    render::MapMarker marker = FishMarker;
    marker.feet = fish.position;
    marker.width = std::max(FishMinWidth, static_cast<float>(fish.size) * FishWidthPerSize);
    marker.color.a = FishShadowAlpha * static_cast<float>(fish.opacity) /
                     static_cast<float>(game::fishing::OpaqueFish);
    markers.push_back(marker);
  }
  if (water.bobberShown()) {
    render::MapMarker marker = BobberMarker;
    marker.feet = water.bobber();
    markers.push_back(marker);
  }
  if (const std::optional<glm::vec3> bait = water.bait()) {
    render::MapMarker marker = BaitMarker;
    marker.feet = *bait;
    markers.push_back(marker);
  }
  scene.setExtraMarkers(std::move(markers));
}

void FishingScreen::showGauge() {
  const game::fishing::Tension& tension = fishing_->fishing()->tension();
  const bool shown = tension.shown && tension.limit > 0;
  if (shown) {
    // FISH_updateRodPose: from -limit (slack) to +limit (about to snap).
    const double level =
        static_cast<double>(tension.level + tension.limit) / static_cast<double>(2 * tension.limit);
    ui_->setSetting(TensionSetting, std::clamp(level, 0.0, 1.0));
  }
  if (shown != gaugeShown_) {
    gaugeShown_ = shown;
    if (shown) {
      ui_->show(FishingHudDocument, false);
    } else {
      ui_->hide(FishingHudDocument);
    }
  }
}

} // namespace dwe::app
