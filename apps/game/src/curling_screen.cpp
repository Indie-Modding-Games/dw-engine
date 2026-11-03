#include "curling_screen.hpp"

#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/math.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/render/map_camera.hpp>
#include <dwe/render/map_renderer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "field_scene.hpp"

namespace dwe::app {

namespace {

using game::curling::CurlingEvent;
using game::curling::Phase;

// kar.c: KAR_tick runs once a frame of the original's 20 a second.
constexpr double CurlingFrameSeconds = 1.0 / 20.0;
// A stone's marker: as wide as two stones touch (KAR_updateCollisions: 0x96 apart), low.
constexpr float StoneWidth = 150.0F;
constexpr float StoneHeight = 40.0F;
constexpr float AimWidth = 40.0F;
constexpr float AimHeight = 20.0F;
// How far ahead of the stone the aim's marker stands.
constexpr float AimReach = 450.0F;
// The angles of the original: 4096 a turn (rcos, rsin).
constexpr double AngleUnits = 4096.0;
constexpr glm::vec4 PlayerColor{0.25F, 0.55F, 1.0F, 1.0F};
constexpr glm::vec4 OpponentColor{1.0F, 0.3F, 0.25F, 1.0F};
constexpr glm::vec4 ObstacleColor{0.6F, 0.6F, 0.65F, 1.0F};
constexpr glm::vec4 SelectedColor{1.0F, 0.85F, 0.2F, 1.0F};
// A pegged stone (type 3 that stopped) is drawn darker.
constexpr float PeggedShade = 0.6F;

constexpr std::string_view PlayerScoreSetting = "curling_hud.player";
constexpr std::string_view OpponentScoreSetting = "curling_hud.opponent";
constexpr std::string_view StatusSetting = "curling_hud.status";
constexpr std::string_view PromptSetting = "curling_hud.prompt";
constexpr std::string_view PowerSetting = "curling_hud.power";
// KAR_tickHintBox: the hint page's four lines (the speaker's name, then three), as text keys.
constexpr std::array<std::string_view, 4> HintSettings{
    "curling_hud.hint_name", "curling_hud.hint_1", "curling_hud.hint_2", "curling_hud.hint_3"};
// KAR_drawHintPagePenguinmon, KAR_drawHintPageMetalMamemon: the opponent's lines among the texts
// (KAR_D_8005AF58, KAR_D_8005B318), four a page.
constexpr std::string_view FirstOpponentLines = "kar.penguinmon_dialogue.{:04}";
constexpr std::string_view SecondOpponentLines = "kar.metalmamemon_dialogue.{:04}";
constexpr std::int32_t HintLines = 4;
constexpr double Hidden = -1.0;

glm::vec4 colorOf(std::size_t team) {
  switch (team) {
  case game::curling::PlayerTeam:
    return PlayerColor;
  case game::curling::OpponentTeam:
    return OpponentColor;
  default:
    return ObstacleColor;
  }
}

// Whether the stone is the one a thrower holds or throws (KAR_renderScene puts it in the hand).
// A point of the rink (the match keeps the original's axes: Z into the screen) on the world's
// ground.
glm::vec3 worldOf(std::int32_t x, std::int32_t z, float ground) {
  glm::vec3 world =
      core::fromOriginalAxes(glm::vec3{static_cast<float>(x), 0.0F, static_cast<float>(z)});
  world.y = ground;
  return world;
}

bool inHand(const game::curling::Match& match, std::size_t team, std::size_t index) {
  const Phase phase = match.phase();
  return std::cmp_equal(match.throwingTeam(), team) && match.selectedStone() == index &&
         phase >= Phase::Aim && phase <= Phase::Throwing;
}

std::string_view statusOf(const game::curling::Match& match) {
  if (match.hint()) {
    return "curling.status.rules";
  }
  const bool player = match.throwingTeam() == static_cast<std::int32_t>(game::curling::PlayerTeam);
  switch (match.phase()) {
  case Phase::Ready:
    return match.readyShown() ? "curling.status.ready" : "";
  case Phase::Select:
    return player ? "curling.status.select" : "curling.status.opponent";
  case Phase::Aim:
    return player ? "curling.status.aim" : "curling.status.opponent";
  case Phase::Power:
    return player ? "curling.status.power" : "curling.status.opponent";
  case Phase::OpponentPlan:
    return "curling.status.opponent";
  case Phase::Release:
  case Phase::Throwing:
  case Phase::Rolling:
    return "curling.status.rolling";
  case Phase::Rings:
  case Phase::Tally:
    return "curling.status.tally";
  case Phase::Demo:
  case Phase::DemoThrow:
  case Phase::DemoCamera:
    return "curling.status.rules";
  default:
    return "";
  }
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
  showMarkers(scene);
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
  const game::curling::Match* match = curling_->match();
  if (match == nullptr || match->finished()) {
    end(scene);
    return;
  }
  showMarkers(scene);
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
  default:
    // The throwers' animations and the rolling sound's stop are not played yet
    // (docs/modulos/game/curling.md: Hueco).
    break;
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

void CurlingScreen::showMarkers(FieldScene& scene) const {
  const game::curling::Match* match = curling_->match();
  if (match == nullptr) {
    scene.setExtraMarkers({});
    return;
  }
  const float ground = scene.field().player().position.y;
  std::vector<render::MapMarker> markers;
  const auto& teams = match->teams();
  for (std::size_t team = 0; team < teams.size(); ++team) {
    for (std::size_t index = 0; index < teams[team].stones.size(); ++index) {
      const game::curling::Stone& stone = teams[team].stones.at(index);
      const bool held = inHand(*match, team, index);
      if (stone.hidden() || (!stone.inPlay() && !held)) {
        continue;
      }
      render::MapMarker marker;
      marker.feet = worldOf(stone.position.x, stone.position.z, ground);
      marker.width = StoneWidth;
      marker.height = StoneHeight;
      marker.color = held ? SelectedColor : colorOf(team);
      if (stone.pegged()) {
        marker.color = glm::vec4{glm::vec3{marker.color} * PeggedShade, marker.color.a};
      }
      markers.push_back(marker);
      if (held && match->phase() == Phase::Aim) {
        const double turn = 2.0 * std::numbers::pi * match->aimAngle() / AngleUnits;
        render::MapMarker aim;
        // KAR_beginThrow: the stone runs along (rcos, rsin) of the angle, on the original's axes.
        aim.feet = marker.feet + core::fromOriginalAxes(
                                     glm::vec3{static_cast<float>(std::cos(turn)) * AimReach, 0.0F,
                                               static_cast<float>(std::sin(turn)) * AimReach});
        aim.width = AimWidth;
        aim.height = AimHeight;
        aim.color = SelectedColor;
        markers.push_back(aim);
      }
    }
  }
  scene.setExtraMarkers(std::move(markers));
}

void CurlingScreen::showHud() {
  const game::curling::Match* match = curling_->match();
  if (match == nullptr) {
    return;
  }
  const auto& teams = match->teams();
  ui_->setSetting(PlayerScoreSetting, static_cast<double>(teams[game::curling::PlayerTeam].score));
  ui_->setSetting(OpponentScoreSetting,
                  static_cast<double>(teams[game::curling::OpponentTeam].score));
  ui_->setSetting(StatusSetting, std::string{statusOf(*match)});
  const std::optional<std::int32_t> option = match->promptOption();
  ui_->setSetting(PromptSetting, option ? static_cast<double>(*option) : Hidden);
  const bool gauge = match->phase() == Phase::Power &&
                     match->throwingTeam() == static_cast<std::int32_t>(game::curling::PlayerTeam);
  const std::int32_t limit = std::max(curling_->powerLimit(), 1);
  ui_->setSetting(PowerSetting, gauge ? static_cast<double>(match->power()) / limit : Hidden);
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

void CurlingScreen::end(FieldScene& scene) {
  showRings(scene, 0);
  running_ = false;
  scene.setExtraMarkers({});
  ui_->hide(CurlingHudDocument);
  if (auto result = curling_->finish(); !result) {
    core::logWarning("[port:curling] KAR_finishMatch (kar.c:945): {}", result.error().message);
  }
  over_ = true;
}

} // namespace dwe::app
