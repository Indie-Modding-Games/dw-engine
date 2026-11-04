#include "fishing_screen.hpp"

#include <dwe/assets/fishing_table.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/screens/name_keys.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <utility>

#include "common.hpp"
#include "field_scene.hpp"
#include "fishing_look.hpp"
#include "messages.hpp"
#include "scene_prims.hpp"

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
// Without the hand that holds the rod (fishingRodPose), its tip is taken this far ahead of the
// hand and this high above it.
constexpr float RodReach = 160.0F;
constexpr float RodRise = 120.0F;
// A hand where the model has no such bone.
constexpr float HandHeight = 100.0F;

// The map's fishing image (ETCDAT/FISHING.TIM, FISHING2.TIM): 256 x 116 texels.
constexpr glm::vec2 FishingImageSize{256.0F, 116.0F};
// FISH_drawBaitSprite: the item's icon of ETCDAT/ITEM.TIM (16 x 16 cells in rows of 16, 15 texels
// drawn), a square facing the screen of the map's splash size to each side of the bait.
constexpr std::string_view ItemImage = "texture/etcdat/item";
constexpr glm::vec2 ItemImageSize{256.0F, 128.0F};
constexpr std::int32_t IconSide = 16;
constexpr std::int32_t IconsPerRow = 16;
constexpr float IconTexels = 15.0F;
// FISH_loadSeadramonModel: ETCDAT/SEA.TMD, five objects, one a piece of the body.
constexpr std::string_view SeaSerpentModel = "model/etcdat/sea";

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

// A quad of the fishing image's rectangle, corners in POLY_FT4 order (top left, top right, bottom
// left, bottom right).
ScenePrim spriteQuad(const game::fishing::FishSprite& sprite, rhi::TextureId texture) {
  const glm::vec2 from = glm::vec2{sprite.texel} / FishingImageSize;
  const glm::vec2 to = glm::vec2{sprite.texel + sprite.size} / FishingImageSize;
  ScenePrim prim{.shape = ScenePrim::Shape::Quad, .texture = texture};
  prim.corners[0].uv = from;
  prim.corners[1].uv = {to.x, from.y};
  prim.corners[2].uv = {from.x, to.y};
  prim.corners[3].uv = to;
  return prim;
}

// The fishing image's sprites on the water: FISH_drawRipples, then FISH_drawMarkers.
void addWaterSprites(std::vector<ScenePrim>& prims, FieldScene& scene,
                     const game::fishing::Fishing& water, const assets::FishingMapEntry& map,
                     const ScenePrimFrame& frame) {
  if (map.image.empty()) {
    return;
  }
  // FISH_drawRipple: a card standing on the water along x, semi-transparent adding (flags 5),
  // sorted a little nearer than its depth.
  if (const std::optional<rhi::TextureId> rings = scene.stpSceneImage(map.image, 0)) {
    for (const auto& slot : water.ripples().slots()) {
      const std::optional<game::fishing::FishSprite> sprite =
          slot ? game::fishing::rippleSprite(*slot) : std::nullopt;
      if (!sprite) {
        continue;
      }
      const float unit = game::fishing::rippleUnitsPerTexel(map.scale, slot->scale);
      ScenePrim prim = spriteQuad(*sprite, *rings);
      prim.blend = ScenePrimBlend::Additive;
      prim.stpMask = true;
      const glm::vec2 left = glm::vec2{-sprite->origin} * unit;
      const glm::vec2 right = glm::vec2{sprite->size - sprite->origin} * unit;
      for (std::size_t corner = 0; corner < 4; ++corner) {
        // The original's y points down.
        const glm::vec3 offset{(corner % 2) == 0 ? left.x : right.x, 0.0F, 0.0F};
        const float down = corner < 2 ? left.y : right.y;
        prim.corners.at(corner).clip = frame.clipNearer(
            slot->position + offset - glm::vec3{0.0F, down, 0.0F}, game::fishing::RippleNearer);
      }
      prims.push_back(prim);
    }
  }
  // FISH_drawMarker: the heart or the cross at the fish's point on the screen, in screen pixels,
  // in front of everything (ordering table 0x42).
  const std::string markers = std::format("{}?palette={}", map.image, 4);
  if (const std::optional<rhi::TextureId> marks = scene.sceneImage(markers)) {
    for (const auto& slot : water.markers().slots()) {
      if (!slot || frame.depth(slot->position) <= 0.0F) {
        continue;
      }
      const game::fishing::FishSprite sprite = game::fishing::markerSprite(*slot);
      ScenePrim prim = spriteQuad(sprite, *marks);
      const glm::vec4 at = clipInFront(frame.clip(slot->position));
      const glm::vec2 left = -glm::vec2{sprite.origin};
      const glm::vec2 right = glm::vec2{sprite.size - sprite.origin};
      for (std::size_t corner = 0; corner < 4; ++corner) {
        prim.corners.at(corner).clip = at;
        prim.corners.at(corner).offset = {(corner % 2) == 0 ? left.x : right.x,
                                          corner < 2 ? left.y : right.y};
      }
      prims.push_back(prim);
    }
  }
}

// FISH_drawBaitSprite: the bait, or the catch on its way to the hand, as its item's icon while it
// is above the water.
void addBait(std::vector<ScenePrim>& prims, FieldScene& scene, const assets::ItemEntry& item,
             glm::vec3 position, float half, const ScenePrimFrame& frame) {
  if (!item.iconPalette || position.y <= 0.0F || frame.depth(position) <= 0.0F) {
    return;
  }
  const std::optional<rhi::TextureId> icons =
      scene.sceneImage(std::format("{}?palette={}", ItemImage, *item.iconPalette));
  if (!icons) {
    return;
  }
  const glm::vec2 texel{static_cast<float>((item.index % IconsPerRow) * IconSide),
                        static_cast<float>((item.index / IconsPerRow) * IconSide)};
  const glm::vec2 from = texel / ItemImageSize;
  const glm::vec2 to = (texel + glm::vec2{IconTexels}) / ItemImageSize;
  // A square parallel to the screen: the camera's right and up in the world.
  const glm::vec3 right{frame.view[0][0], frame.view[1][0], frame.view[2][0]};
  const glm::vec3 up{frame.view[0][1], frame.view[1][1], frame.view[2][1]};
  ScenePrim prim{.shape = ScenePrim::Shape::Quad, .texture = *icons};
  const std::array<glm::vec2, 4> uvs{from, glm::vec2{to.x, from.y}, glm::vec2{from.x, to.y}, to};
  for (std::size_t corner = 0; corner < 4; ++corner) {
    const float x = (corner % 2) == 0 ? -half : half;
    const float y = corner < 2 ? half : -half;
    prim.corners.at(corner).clip = frame.clip(position + (right * x) + (up * y));
    prim.corners.at(corner).uv = uvs.at(corner);
  }
  prims.push_back(prim);
}

// The map's screen as the fishing sees it (FISH_projectPoint): background pixels from its optical
// centre, which is the middle of the background. The fishing's points on the screen are anywhere on
// the background (FISH_screenLeft(0) to FISH_screenRight(0x140)), not only in the visible window:
// FISH_randomPointInSpot places fish in spots the view does not show.
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
  [[nodiscard]] glm::vec2 screenMin() const override { return -centre_; }
  [[nodiscard]] glm::vec2 screenMax() const override { return centre_; }
  // The view's centre keeps the visible window on the background; on an axis the background does
  // not fill, it stays in the middle.
  [[nodiscard]] game::fishing::ViewRange viewRange() const override {
    const glm::vec2 half = camera_->visibleSize() * 0.5F;
    const glm::vec2 background = camera_->desc().backgroundSize;
    game::fishing::ViewRange range;
    for (int axis = 0; axis < 2; ++axis) {
      if (background[axis] <= 2.0F * half[axis]) {
        continue;
      }
      range.min[axis] = static_cast<std::int32_t>(std::ceil(half[axis] - centre_[axis]));
      range.max[axis] =
          static_cast<std::int32_t>(std::floor(background[axis] - half[axis] - centre_[axis]));
    }
    return range;
  }
  [[nodiscard]] glm::ivec2 viewCentre() const override {
    const glm::vec2 centre = camera_->scroll() + (camera_->visibleSize() * 0.5F) - centre_;
    return {static_cast<std::int32_t>(std::lround(centre.x)),
            static_cast<std::int32_t>(std::lround(centre.y))};
  }

private:
  const render::MapCamera* camera_;
  glm::vec2 centre_;
};

} // namespace

FishingScreen::FishingScreen(session::FishingAdapter& fishing,
                             const session::InventoryAdapter& inventory,
                             session::GameSession& session, ui::Ui& ui,
                             game::story::SoundPort* sound, session::StoryAdapter* story)
    : fishing_{&fishing}, inventory_{&inventory}, session_{&session}, ui_{&ui}, sound_{sound},
      story_{story} {}

bool FishingScreen::active() const {
  const game::fishing::Fishing* water = fishing_->fishing();
  return (water != nullptr && water->fishing()) || noticeShown_;
}

void FishingScreen::enterMap(FieldScene& scene, std::string_view mapId) {
  fishing_->enterMap(mapId);
  scene.setPlayerClip(std::nullopt);
  scene.setExtraMarkers({});
  scene.setMinigamePrims({});
  scene.setCameraPixel(std::nullopt);
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
  cameraPressed_ = cameraPressed_ || (buttons.cameraDown && !cameraDown_);
  confirmDown_ = buttons.confirmDown;
  cameraDown_ = buttons.cameraDown;
  buttons_ = buttons;
  game::fishing::Fishing* water = fishing_->fishing();
  if (water == nullptr) {
    return;
  }
  frameSeconds_ += seconds;
  while (frameSeconds_ >= FishingFrameSeconds && water->fishing()) {
    frameSeconds_ -= FishingFrameSeconds;
    if (!noticeShown_) {
      frame(scene);
    } else if (const render::MapCamera* camera = scene.camera()) {
      // FISH_scrollViewToTarget: the fishing waits on the notice while the view turns to it.
      water->scrollToNotice(noticeTarget_, CameraView{*camera});
    }
  }
  if (!water->fishing()) {
    frameSeconds_ = 0.0;
  }
  showCamera(scene);
  showMarkers(scene);
  showPrims(scene);
  showGauge();
}

game::fishing::FishingInput FishingScreen::inputOf(const FieldScene& scene) const {
  const game::field::CharacterState& player = scene.field().player();
  const glm::vec3 facing = assets::forwardOfYaw(player.yaw);
  const glm::vec3 hand = scene.playerNodePoint(HandNode, GripOffset)
                             .value_or(player.position + glm::vec3{0.0F, HandHeight, 0.0F});
  const game::fishing::Fishing& water = *fishing_->fishing();
  // FISH_updateRodPose: the line starts at the rod's tip.
  const std::optional<game::fishing::RodPose> rod = fishingRodPose(scene, water);
  const glm::vec3 tip = rod ? rod->tip : hand + facing * RodReach + glm::vec3{0.0F, RodRise, 0.0F};
  return {.confirmPressed = confirmPressed_,
          .confirmHeld = confirmDown_,
          .confirmReleased = confirmReleased_,
          .cancelPressed = cancelPressed_,
          .cameraPressed = cameraPressed_,
          .cameraHeld = cameraDown_,
          .turnLeft = buttons_.left,
          .turnRight = buttons_.right,
          .lookUp = buttons_.up,
          .lookDown = buttons_.down,
          .animationPlaying = scene.playerClipPlaying(),
          .lineStart = tip,
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
  cameraPressed_ = false;
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
  glm::vec3 catchTarget = fishing_->fishing()->bobber();
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
                          .size = 0,
                          .target = event.position});
      break;
    case FishingEventKind::FishCaught:
    case FishingEventKind::JunkCaught:
      catchTarget = event.position;
      break;
    case FishingEventKind::OpenBaitMenu:
      openBaitMenu();
      break;
    case FishingEventKind::Ended:
      scene.setPlayerClip(std::nullopt);
      break;
    case FishingEventKind::SeaSerpentLanded:
      // tamerTickFishing: triggerSeadramonCutscene (script_common.c:3057) calls the map-head
      // section; the fishing stays off on the map (setFishingDisabled).
      if (story_ != nullptr) {
        const auto outcome = story_->request({.script = game::story::StoryRunner::MapHeadScript,
                                              .section = static_cast<std::uint16_t>(event.value)},
                                             game::story::StartTiming::NextFrame);
        core::logDebug("[port:fishing] triggerSeadramonCutscene (script_common.c:3057): section "
                       "{} of the map-head script {}",
                       event.value,
                       outcome == game::story::RequestOutcome::Accepted ? "asked for" : "dropped");
      } else {
        core::logInfo("{}", messages::SeaSerpentScene);
      }
      break;
    default:
      break;
    }
  }
  const screens::NameKeys names{nullptr, &inventory_->items(), nullptr};
  for (const session::FishingCatch& caught : fishing_->apply(*session_, out)) {
    const bool fish = caught.kind == session::FishingCatch::Kind::Fish;
    std::string_view text = fish ? CaughtLetGoText : StuckBagFullText;
    if (caught.kept) {
      text = fish ? CaughtText : StuckText;
    }
    notices_.push_back({.text = std::string{text},
                        .item = names.item(caught.item),
                        .size = fish ? caught.size : 0,
                        .target = catchTarget});
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
  noticeTarget_ = notice.target;
}

void FishingScreen::showCamera(FieldScene& scene) const {
  const render::MapCamera* camera = scene.camera();
  const std::optional<glm::ivec2>& centre = fishing_->fishing()->cameraCentre();
  if (camera == nullptr || !centre) {
    scene.setCameraPixel(std::nullopt);
    return;
  }
  scene.setCameraPixel(glm::vec2{*centre} + (camera->desc().backgroundSize * 0.5F));
}

void FishingScreen::showPrims(FieldScene& scene) const {
  const game::fishing::Fishing& water = *fishing_->fishing();
  const render::MapCamera* camera = scene.camera();
  std::vector<ScenePrim> prims;
  if (camera == nullptr || !water.fishing()) {
    scene.setMinigamePrims(std::move(prims));
    return;
  }
  // FISH_drawFishingLine: from the rod's tip to the bobber, cut to the visible window, in the
  // tension's colour.
  if (water.lineShown()) {
    const std::optional<render::BackgroundPoint> from =
        camera->projectToBackground(water.lineStart());
    const std::optional<render::BackgroundPoint> to = camera->projectToBackground(water.bobber());
    if (from && to) {
      const glm::vec2 min = camera->scroll();
      const glm::vec2 max = min + camera->visibleSize();
      const glm::vec4 color{glm::vec3{water.tension().color} / 255.0F, 1.0F};
      for (const auto& piece :
           game::fishing::fishingLine({.pixel = from->pixel, .depth = from->viewDepth},
                                      {.pixel = to->pixel, .depth = to->viewDepth}, min, max)) {
        ScenePrim line{
            .shape = ScenePrim::Shape::Line, .color = color, .texture = scene.whiteTexture()};
        for (std::size_t end = 0; end < piece.size(); ++end) {
          line.corners.at(end).clip =
              camera->backgroundToClip(piece.at(end).pixel, piece.at(end).depth);
        }
        prims.push_back(line);
      }
    }
  }
  // FISH_drawSeadramon: the sea serpent's five pieces, each turned as the head was a frame later
  // than the one before, drawn subtractive. Its faces read an 8 x 8 corner of the texture page that
  // is empty in the model's own image (the original finds it filled in its video memory); the
  // shape measured on the water is black, the most a subtraction takes: white is subtracted.
  const std::vector<std::vector<BodyFace>>* pieces = scene.sceneModelFaces(SeaSerpentModel);
  const ScenePrimFrame frame{
      .view = camera->view(), .projection = camera->projection(), .pixelScale = camera->scale()};
  const auto drawSerpent = [&](const game::fishing::Fish& fish) {
    if (fish.kind != game::fishing::SeaSerpentKind || pieces == nullptr) {
      return;
    }
    const auto body = game::fishing::seaSerpentBody(fish.position, fish.turns);
    for (std::size_t index = 0; index < body.size() && index < pieces->size(); ++index) {
      const glm::mat4 place = glm::rotate(glm::translate(glm::mat4{1.0F}, body.at(index).position),
                                          body.at(index).yaw, glm::vec3{0.0F, 1.0F, 0.0F});
      for (const BodyFace& face : pieces->at(index)) {
        ScenePrim prim{.shape =
                           face.count == 4 ? ScenePrim::Shape::Quad : ScenePrim::Shape::Triangle,
                       .texture = scene.whiteTexture(),
                       .blend = ScenePrimBlend::Subtractive};
        for (std::size_t corner = 0; corner < face.count; ++corner) {
          prim.corners.at(corner).clip =
              frame.clip(glm::vec3{place * glm::vec4{face.corners.at(corner), 1.0F}});
        }
        prims.push_back(prim);
      }
    }
  };
  for (const game::fishing::Fish& fish : water.fish()) {
    drawSerpent(fish);
  }
  if (water.hooked()) {
    drawSerpent(*water.hooked());
  }
  addFishingFish(scene, water, frame, prims);
  if (const assets::FishingMapEntry* map = fishing_->map()) {
    addWaterSprites(prims, scene, water, *map, frame);
  }
  addFishingRod(scene, water, frame, prims);
  if (const assets::FishingMapEntry* map = fishing_->map()) {
    const std::optional<assets::ItemHandle> item = fishing_->itemOfNumber(water.baitItem());
    if (const std::optional<glm::vec3> bait = water.bait(); bait && item) {
      addBait(prims, scene, inventory_->items().at(*item), *bait, static_cast<float>(map->splash),
              frame);
    }
  }
  scene.setMinigamePrims(std::move(prims));
}

void FishingScreen::showMarkers(FieldScene& scene) const {
  // The fish, the rod and the bobber are drawn as the original's (showPrims).
  scene.setExtraMarkers({});
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
