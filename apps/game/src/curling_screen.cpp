#include "curling_screen.hpp"

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/math.hpp>
#include <dwe/core/original_math.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/render/map_camera.hpp>
#include <dwe/render/map_renderer.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "field_scene.hpp"
#include "recruit_bar.hpp"

namespace dwe::app {

namespace {

using game::curling::CurlingEvent;

// kar.c: KAR_tick runs once a frame of the original's 20 a second.
constexpr double CurlingFrameSeconds = 1.0 / 20.0;
// KAR_start reads the stones' model (\ETCDAT\KARRING.TMD); KAR_renderScene draws one object of it
// a stone (stoneModelObject).
constexpr std::string_view StoneModel = "model/etcdat/karring";
// KAR_renderScene: a waiting stone is drawn with the racks' own view, the eye 3320 in front of the
// racks (KAR_D_8005B580) at GsSetProjection(0x200), over everything the map draws (its own ordering
// table sorted in front). Here the stone goes on the ray through that point of the screen, part of
// the way from the camera to the ground, at the size that view gives it.
constexpr float RackDistance = 3320.0F;
constexpr float RackProjection = 512.0F;
constexpr float RackDepth = 0.5F;
constexpr float RackTilt =
    0x96 * 2.0F * std::numbers::pi_v<float> / static_cast<float>(core::OriginalAnglePerTurn);
// KAR_renderScene: a stone in a hand is the hand's node (the player's 9, the second opponent's 4)
// plus 0x78 down; the first opponent's stands 0x50 to its left on the ground (the match places it).
constexpr std::uint32_t PlayerHandNode = 9;
constexpr std::uint32_t SecondOpponentHandNode = 4;
constexpr float HeldDrop = 0x78;
// frzl17's NPCs (script 131): the opponent the story spawns has script id 5, both of them.
constexpr std::int32_t OpponentScriptId = 5;
// The characters of the match's Animation events and facings.
constexpr std::int32_t ThrowerPlayer = 0;
constexpr std::int32_t ThrowerOpponent = 1;
constexpr std::int32_t ThrowerPartner = 2;
// KAR_renderAimArrow's POLY_FT4s: the image at VRAM (640, 0) with the CLUT at (0x80, 0x1E6), the
// rink's first 4-bit image and its ninth palette (overworld.c: loadMapImage2 puts palette i at
// x = 16 i); a dot of 16 x 16 at texels (0x18, 0x28), the head at (0x28, 0x28), 16 pixels square
// on the screen about the point.
constexpr std::string_view ArrowImage = "map/frzl17/texture4/000";
constexpr glm::vec2 ArrowImageSize{256.0F, 88.0F};
constexpr std::size_t ArrowPalette = 8;
constexpr glm::vec2 DotTexel{0x18, 0x28};
constexpr glm::vec2 HeadTexel{0x28, 0x28};
constexpr float ArrowHalf = 8.0F;
constexpr float ArrowSide = 16.0F;
// The head's quad is 15 texels unless it points straight (setUVWH 16 at 0x800).
constexpr std::int32_t StraightHead = 0x800;
// A primitive's colour over 0x80 draws the texture as is.
constexpr float ColorOne = 128.0F;

// The HUD document's settings (ui/curling_hud/logic.lua).
constexpr std::array<std::array<std::string_view, 2>, 2> ScoreSettings{{
    {"curling_hud.score_0_tens", "curling_hud.score_0_units"},
    {"curling_hud.score_1_tens", "curling_hud.score_1_units"},
}};
constexpr std::string_view SecondOpponentSetting = "curling_hud.second";
constexpr std::string_view ReadySetting = "curling_hud.ready";
constexpr std::string_view GaugeTeamSetting = "curling_hud.gauge_team";
constexpr std::string_view ChevronsSetting = "curling_hud.chevrons";
constexpr std::string_view HandTeamSetting = "curling_hud.hand_team";
constexpr std::string_view HandRowSetting = "curling_hud.hand_row";
constexpr std::string_view PromptSetting = "curling_hud.prompt";
// The recruit bar's revision (curling_recruit_bar.hpp): the document redraws it when it changes.
constexpr std::string_view RecruitRevisionSetting = "curling_recruit.revision";
// KAR_tickHintBox: the hint page's four lines (the speaker's name, then three), as text keys.
constexpr std::array<std::string_view, 4> HintSettings{
    "curling_hud.hint_name", "curling_hud.hint_1", "curling_hud.hint_2", "curling_hud.hint_3"};
// KAR_drawHintPagePenguinmon, KAR_drawHintPageMetalMamemon: the opponent's lines among the texts
// (KAR_D_8005AF58, KAR_D_8005B318), four a page.
constexpr std::string_view FirstOpponentLines = "kar.penguinmon_dialogue.{:04}";
constexpr std::string_view SecondOpponentLines = "kar.metalmamemon_dialogue.{:04}";
constexpr std::int32_t HintLines = 4;
constexpr double Hidden = -1.0;

// Whether the stone is the one a thrower holds or throws (KAR_renderScene puts it in the hand).
// A point of the rink (the match keeps the original's axes: Z into the screen) on the world's
// ground.
glm::vec3 worldOf(std::int32_t x, std::int32_t z, float ground) {
  glm::vec3 world =
      core::fromOriginalAxes(glm::vec3{static_cast<float>(x), 0.0F, static_cast<float>(z)});
  world.y = ground;
  return world;
}

// A stone of the model: its object shown, the others hidden (one joint each).
FieldScene::PropPlace stonePlace(glm::mat4 world, std::size_t object) {
  std::vector<glm::mat4> joints(game::curling::StoneModelObjects, glm::mat4{0.0F});
  joints.at(object) = glm::mat4{1.0F};
  return {.world = world, .joints = std::move(joints), .tint = glm::vec4{1.0F}};
}

glm::mat4 turned(glm::vec3 at, std::int32_t rotation, float scale = 1.0F) {
  const glm::mat4 place =
      glm::rotate(glm::translate(glm::mat4{1.0F}, at), assets::yawFromOriginal(rotation),
                  glm::vec3{0.0F, 1.0F, 0.0F});
  return glm::scale(place, glm::vec3{scale});
}

// Where a waiting stone is drawn: its point of the racks' view on the screen (x across, its height
// down, about the screen's centre), as the map's camera shows a stone there.
std::optional<glm::mat4> rackPlace(const render::MapCamera& camera,
                                   const game::curling::Stone& stone, float ground) {
  const float pixels = RackProjection / RackDistance;
  const glm::vec2 offset{static_cast<float>(stone.position.x) * pixels,
                         static_cast<float>(stone.height) * pixels};
  const glm::vec2 window = (camera.viewportSize() * 0.5F) + (offset * camera.scale());
  const std::optional<glm::vec3> under = camera.pickGround(window, ground);
  if (!under) {
    return std::nullopt;
  }
  // The map camera's pixels a unit across the screen there.
  const glm::mat4 eyeSpace = glm::inverse(camera.view());
  const glm::vec3 eye{eyeSpace[3]};
  const glm::vec3 across = glm::normalize(glm::vec3{eyeSpace[0]});
  const std::optional<glm::vec2> from = camera.worldToWindow(*under);
  const std::optional<glm::vec2> to = camera.worldToWindow(*under + (across * 100.0F));
  if (!from || !to) {
    return std::nullopt;
  }
  const float mapPixels = glm::length(*to - *from) / camera.scale() / 100.0F;
  if (mapPixels <= 0.0F) {
    return std::nullopt;
  }
  const glm::vec3 at = eye + ((*under - eye) * RackDepth);
  // The racks' view looks straight at them: the stone faces the map camera as it faces that view,
  // tipped 0x96 about the screen's x and turned by its spin (KAR_setupMatch, KAR_renderScene).
  glm::mat4 place = glm::translate(glm::mat4{1.0F}, at) * glm::mat4{glm::mat3{eyeSpace}};
  place = glm::rotate(place, RackTilt, glm::vec3{1.0F, 0.0F, 0.0F});
  place = glm::rotate(place, assets::yawFromOriginal(stone.spin), glm::vec3{0.0F, 1.0F, 0.0F});
  return glm::scale(place, glm::vec3{pixels / mapPixels * RackDepth});
}

// The first line of a hint page among the opponent's lines (KAR_D_8005B04C: the groups' pages one
// after the other).
std::int32_t hintLine(std::int32_t group, std::int32_t page) {
  std::int32_t pages = 0;
  for (std::int32_t index = 0; index < group; ++index) {
    pages += game::curling::HintPages.at(static_cast<std::size_t>(index));
  }
  return (pages + page - 1) * HintLines;
}

} // namespace

CurlingScreen::CurlingScreen(session::CurlingAdapter& curling, session::GameSession& session,
                             ui::Ui& ui, game::story::SoundPort* sound)
    : curling_{&curling}, session_{&session}, ui_{&ui}, sound_{sound} {}

bool CurlingScreen::start(FieldScene& scene) {
  if (auto begun = curling_->begin(*session_); !begun) {
    core::logWarning("[port:curling] KAR_start (kar.c:778): {}; the scene ends at once",
                     begun.error().message);
    return false;
  }
  running_ = true;
  over_ = false;
  frameSeconds_ = 0.0;
  buttons_ = {};
  ui_->show(CurlingHudDocument, false);
  shownRings_ = UnknownRings;
  showHud();
  showStones(scene);
  showRings(scene, 0);
  return true;
}

void CurlingScreen::press(const input::InputState& input) {
  if (!running_) {
    return;
  }
  buttons_.up = input.isDown("move_up");
  buttons_.down = input.isDown("move_down");
  buttons_.left = input.isDown("move_left");
  buttons_.right = input.isDown("move_right");
  buttons_.upPressed = buttons_.upPressed || input.wasPressed("move_up");
  buttons_.downPressed = buttons_.downPressed || input.wasPressed("move_down");
  buttons_.leftPressed = buttons_.leftPressed || input.wasPressed("move_left");
  buttons_.rightPressed = buttons_.rightPressed || input.wasPressed("move_right");
  buttons_.confirmPressed = buttons_.confirmPressed || input.wasPressed("confirm");
}

void CurlingScreen::tick(FieldScene& scene, double seconds) {
  if (!running_) {
    return;
  }
  frameSeconds_ += seconds;
  while (running_ && frameSeconds_ >= CurlingFrameSeconds) {
    frameSeconds_ -= CurlingFrameSeconds;
    frame(scene);
  }
}

bool CurlingScreen::takeOver() {
  return std::exchange(over_, false);
}

void CurlingScreen::frame(FieldScene& scene) {
  placeHands(scene);
  const game::curling::CurlingButtons buttons = buttons_;
  buttons_.upPressed = false;
  buttons_.downPressed = false;
  buttons_.leftPressed = false;
  buttons_.rightPressed = false;
  buttons_.confirmPressed = false;
  // The span lives until the next tick of the match.
  for (const CurlingEvent& event : curling_->play(*session_, buttons)) {
    follow(scene, event);
  }
  faceCharacters(scene);
  const game::curling::Match* match = curling_->match();
  if (match == nullptr || match->finished()) {
    end(scene);
    return;
  }
  showStones(scene);
  showRings(scene, match->litRings());
  showHud();
}

void CurlingScreen::follow(FieldScene& scene, const CurlingEvent& event) {
  switch (event.kind) {
  case CurlingEvent::Kind::Sound:
    if (sound_ != nullptr) {
      sound_->playSound(event.a, event.b);
    }
    break;
  case CurlingEvent::Kind::Camera: {
    // tickCameraMoveTo: the map's camera to the point, at once here.
    game::field::Field& field = scene.field();
    const glm::vec3 ground = field.player().position;
    const glm::vec3 to = worldOf(event.a, event.b, ground.y);
    field.shiftCameraByScript(to - field.scriptCamera().value_or(ground));
    break;
  }
  case CurlingEvent::Kind::Scroll:
    scroll(scene, event.a, event.b);
    break;
  case CurlingEvent::Kind::Follow:
    // moveCameraByDiff: the held camera moves with the thrown stone, without the scroll's clamp.
    scene.field().shiftCameraByScript(core::fromOriginalAxes(
        glm::vec3{static_cast<float>(event.a), 0.0F, static_cast<float>(event.b)}));
    break;
  case CurlingEvent::Kind::RollingSound:
    if (sound_ != nullptr) {
      stopRolling();
      rolling_ = sound_->playHeldSound(event.a, event.b);
    }
    break;
  case CurlingEvent::Kind::SoundStop:
    stopRolling();
    break;
  case CurlingEvent::Kind::Animation:
    animate(scene, event.a, event.b);
    break;
  default:
    break;
  }
}

void CurlingScreen::stopRolling() {
  if (sound_ != nullptr && rolling_) {
    sound_->stopHeldSound(*rolling_);
  }
  rolling_.reset();
}

std::optional<scene::Entity> CurlingScreen::characterOf(const FieldScene& scene,
                                                        std::int32_t who) const {
  const game::field::Field& field = scene.field();
  switch (who) {
  case ThrowerPlayer:
    return field.playerEntity();
  case ThrowerOpponent:
    return field.npcs().entityOf(OpponentScriptId);
  case ThrowerPartner:
    return field.partnerEntity();
  default:
    return std::nullopt;
  }
}

void CurlingScreen::animate(FieldScene& scene, std::int32_t who, std::int32_t animation) {
  // startAnimation on the throwers and the partner, held by the routine's script as the story's
  // clips are: the player and the partner until the script lets go, the opponent until its
  // animation goes back to 0 (StoryStage::animate).
  const std::optional<scene::Entity> entity = characterOf(scene, who);
  if (!entity || animation < 0) {
    return;
  }
  game::field::Field& field = scene.field();
  if (who == ThrowerOpponent) {
    field.npcs().setScriptedAnimation(OpponentScriptId, animation);
    if (animation == 0) {
      field.releaseClipByScript(*entity);
      return;
    }
  }
  field.animateByScript(*entity, static_cast<std::uint32_t>(animation));
}

void CurlingScreen::faceCharacters(FieldScene& scene) const {
  // The facing the match gave each character (rotation.vy), at once.
  const game::curling::Match* match = curling_->match();
  if (match == nullptr) {
    return;
  }
  for (const std::int32_t who : {ThrowerPlayer, ThrowerOpponent, ThrowerPartner}) {
    const std::optional<std::int32_t> facing = match->facing(static_cast<std::size_t>(who));
    if (const std::optional<scene::Entity> entity = characterOf(scene, who); entity && facing) {
      scene.setEntityYaw(*entity, assets::yawFromOriginal(*facing));
    }
  }
}

void CurlingScreen::scroll(FieldScene& scene, std::int32_t dx, std::int32_t dy) {
  // moveCameraByOffset: the view moves by background pixels and stays inside the background
  // (cameraIsAtEdge keeps cameraX in [0, width - 320]); the held camera moves to the ground point
  // that ends up in the centre.
  game::field::Field& field = scene.field();
  const render::MapCamera& camera = scene.mapCamera();
  const glm::vec3 focus = field.scriptCamera().value_or(field.player().position);
  const std::optional<render::BackgroundPoint> at = camera.projectToBackground(focus);
  if (!at) {
    return;
  }
  const glm::vec2 half = render::OriginalScreenSize * 0.5F;
  const glm::vec2 far = glm::max(half, camera.desc().backgroundSize - half);
  const glm::vec2 pixel =
      glm::clamp(at->pixel + glm::vec2{static_cast<float>(dx), static_cast<float>(dy)}, half, far);
  if (const auto ground = camera.pickGround(camera.backgroundToWindow(pixel), focus.y)) {
    field.shiftCameraByScript(*ground - focus);
  }
}

void CurlingScreen::showRings(FieldScene& scene, std::uint32_t lit) {
  // KAR_updateRingMarkers: setMapObjectsFlag shows a lit ring's objects and hides the others.
  if (lit == shownRings_) {
    return;
  }
  const std::span<const game::curling::Ring> rings = curling_->rings();
  for (std::size_t ring = 0; ring < rings.size(); ++ring) {
    const bool shown = ((lit >> ring) & 1U) != 0;
    for (const std::int32_t object : rings[ring].objects) {
      scene.setMapObjectFlag(object, shown ? 0 : assets::MapObjectInstance::HiddenFlag);
    }
  }
  shownRings_ = lit;
}

void CurlingScreen::placeHands(FieldScene& scene) {
  // KAR_renderScene places the stone in a hand where the hand is drawn: the match throws it from
  // there.
  const game::curling::Match* match = curling_->match();
  if (match == nullptr) {
    return;
  }
  const auto ground = [](std::optional<glm::vec3> point) -> std::optional<game::curling::Point> {
    if (!point) {
      return std::nullopt;
    }
    const glm::vec3 original = core::fromOriginalAxes(*point);
    return game::curling::Point{.x = static_cast<std::int32_t>(std::lround(original.x)),
                                .z = static_cast<std::int32_t>(std::lround(original.z))};
  };
  const game::field::Field& field = scene.field();
  const auto player = ground(scene.entityNodePoint(field.playerEntity(), PlayerHandNode));
  // The first opponent holds its stone beside it on the ground (Match::placeHeldStone).
  std::optional<game::curling::Point> opponent;
  const auto rival = field.npcs().entityOf(OpponentScriptId);
  if (rival && match->opponent() == game::curling::Opponent::MetalMamemon) {
    opponent = ground(scene.entityNodePoint(*rival, SecondOpponentHandNode));
  }
  curling_->setHands(player.value_or(game::curling::PlayerMark),
                     opponent.value_or(game::curling::OpponentMark));
}

std::optional<glm::vec3> CurlingScreen::heldPoint(FieldScene& scene, std::size_t team,
                                                  const game::curling::Stone& stone,
                                                  float ground) const {
  const game::curling::Match* match = curling_->match();
  const game::field::Field& field = scene.field();
  std::optional<glm::vec3> hand;
  if (team == game::curling::PlayerTeam) {
    hand = scene.entityNodePoint(field.playerEntity(), PlayerHandNode);
  } else if (match != nullptr && match->opponent() == game::curling::Opponent::MetalMamemon) {
    if (const auto rival = field.npcs().entityOf(OpponentScriptId)) {
      hand = scene.entityNodePoint(*rival, SecondOpponentHandNode);
    }
  }
  if (!hand) {
    return worldOf(stone.position.x, stone.position.z, ground);
  }
  return glm::vec3{hand->x, hand->y - HeldDrop, hand->z};
}

void CurlingScreen::showStones(FieldScene& scene) const {
  const auto model = assets::AssetId::parse(StoneModel);
  const game::curling::Match* match = curling_->match();
  if (!model) {
    return;
  }
  if (match == nullptr) {
    scene.setPosedProps(*model, {});
    scene.setPointQuads({});
    return;
  }
  const float ground = scene.field().player().position.y;
  std::vector<FieldScene::PropPlace> stones;
  const auto& teams = match->teams();
  for (std::size_t team = 0; team < teams.size(); ++team) {
    for (std::size_t index = 0; index < teams[team].stones.size(); ++index) {
      if (!match->stoneDrawn(team, index)) {
        continue;
      }
      const game::curling::Stone& stone = teams[team].stones.at(index);
      const std::size_t object = game::curling::stoneModelObject(team, stone);
      std::optional<glm::mat4> world;
      if (stone.state < 0) {
        world = rackPlace(scene.mapCamera(), stone, ground);
      } else if (stone.state == 0) {
        if (const auto at = heldPoint(scene, team, stone, ground)) {
          world = turned(*at, 0);
        }
      } else {
        world = turned(worldOf(stone.position.x, stone.position.z, ground), stone.spin);
      }
      if (world) {
        stones.push_back(stonePlace(*world, object));
      }
    }
  }
  scene.setPosedProps(*model, std::move(stones));
  showAimArrow(scene, ground);
}

void CurlingScreen::showAimArrow(FieldScene& scene, float ground) const {
  // KAR_renderAimArrow: a dot a point, the last one the head turned along the line, each grey
  // over the texture; one dot left out.
  const game::curling::Match* match = curling_->match();
  std::vector<PointQuad> quads;
  if (match != nullptr && match->aimArrow()) {
    const game::curling::AimArrow& arrow = *match->aimArrow();
    for (std::size_t dot = 0; dot < game::curling::AimDots; ++dot) {
      if (dot == arrow.hidden) {
        continue;
      }
      const game::curling::Point at = arrow.dots.at(dot);
      PointQuad quad{.point = worldOf(at.x, at.z, ground),
                     .image = std::string{ArrowImage},
                     .palette = ArrowPalette,
                     .imageSize = ArrowImageSize};
      const float grey = static_cast<float>(arrow.shades.at(dot)) / ColorOne;
      quad.color = glm::vec4{grey, grey, grey, 1.0F};
      if (dot + 1 < game::curling::AimDots) {
        // setUVWH(0x18, 0x38, 16, -16) on (x-8, y+8), (x+8, y+8), (x-8, y-8), (x+8, y-8).
        quad.offsets = {glm::vec2{-ArrowHalf, ArrowHalf}, glm::vec2{ArrowHalf, ArrowHalf},
                        glm::vec2{-ArrowHalf, -ArrowHalf}, glm::vec2{ArrowHalf, -ArrowHalf}};
        quad.texels = {DotTexel + glm::vec2{0.0F, ArrowSide},
                       DotTexel + glm::vec2{ArrowSide, ArrowSide}, DotTexel,
                       DotTexel + glm::vec2{ArrowSide, 0.0F}};
      } else {
        // The corners (-8, 8), (8, 8), (-8, -8), (8, -8) turned by KAR_rotatePoint.
        const std::int32_t turn = arrow.headTurn;
        const std::int32_t cosine = core::original::rcos(turn);
        const std::int32_t sine = core::original::rsin(turn);
        const auto turned = [&](std::int32_t x, std::int32_t z) {
          return glm::vec2{static_cast<float>(((x * cosine) / 4096) - ((z * sine) / 4096)),
                           static_cast<float>(((x * sine) / 4096) + ((z * cosine) / 4096))};
        };
        const auto half = static_cast<std::int32_t>(ArrowHalf);
        quad.offsets = {turned(-half, half), turned(half, half), turned(-half, -half),
                        turned(half, -half)};
        const float side = turn == StraightHead ? ArrowSide : ArrowSide - 1.0F;
        quad.texels = {HeadTexel, HeadTexel + glm::vec2{side, 0.0F},
                       HeadTexel + glm::vec2{0.0F, side}, HeadTexel + glm::vec2{side, side}};
      }
      quads.push_back(std::move(quad));
    }
  }
  scene.setPointQuads(std::move(quads));
}

void CurlingScreen::showHud() {
  const game::curling::Match* match = curling_->match();
  if (match == nullptr) {
    return;
  }
  const game::curling::HudView hud = match->hud();
  for (std::size_t team = 0; team < ScoreSettings.size(); ++team) {
    for (std::size_t digit = 0; digit < 2; ++digit) {
      ui_->setSetting(ScoreSettings.at(team).at(digit),
                      static_cast<double>(hud.scores.at(team).at(digit)));
    }
  }
  ui_->setSetting(SecondOpponentSetting, hud.secondOpponent ? 1.0 : 0.0);
  ui_->setSetting(ReadySetting, hud.ready ? 1.0 : 0.0);
  ui_->setSetting(GaugeTeamSetting, hud.gauge ? static_cast<double>(hud.gauge->team) : Hidden);
  ui_->setSetting(ChevronsSetting, hud.gauge ? static_cast<double>(hud.gauge->chevrons) : 0.0);
  ui_->setSetting(HandTeamSetting, hud.cursor ? static_cast<double>(hud.cursor->team) : Hidden);
  ui_->setSetting(HandRowSetting, hud.cursor ? static_cast<double>(hud.cursor->row) : 0.0);
  const std::optional<std::int32_t> option = match->promptOption();
  ui_->setSetting(PromptSetting, option ? static_cast<double>(*option) : Hidden);
  showRecruitBar();
  const auto hint = match->hint();
  const std::int32_t first = hint ? hintLine(hint->first, hint->second) : 0;
  for (std::size_t line = 0; line < HintSettings.size(); ++line) {
    std::string key;
    if (hint) {
      const std::int32_t at = first + static_cast<std::int32_t>(line);
      key = match->opponent() == game::curling::Opponent::Penguinmon
                ? std::format(FirstOpponentLines, at)
                : std::format(SecondOpponentLines, at);
    }
    ui_->setSetting(HintSettings.at(line), key);
  }
}

void CurlingScreen::showRecruitBar() {
  const game::curling::CurlingRules* rules = curling_->rules();
  const CurlingRecruitView view =
      rules != nullptr ? curlingRecruitOf(curling_->match(), *rules, recruitBarOn(*ui_))
                       : CurlingRecruitView{};
  if (publishedRecruit_ == view) {
    return;
  }
  screens::publish(*ui_, settingsOf(view), RecruitRevisionSetting);
  publishedRecruit_ = view;
}

void CurlingScreen::end(FieldScene& scene) {
  showRings(scene, 0);
  running_ = false;
  stopRolling();
  if (const auto model = assets::AssetId::parse(StoneModel)) {
    scene.setPosedProps(*model, {});
  }
  scene.setPointQuads({});
  publishedRecruit_.reset();
  screens::publish(*ui_, settingsOf(CurlingRecruitView{}), RecruitRevisionSetting);
  ui_->hide(CurlingHudDocument);
  if (auto result = curling_->finish(); !result) {
    core::logWarning("[port:curling] KAR_finishMatch (kar.c:945): {}", result.error().message);
  }
  over_ = true;
}

} // namespace dwe::app
