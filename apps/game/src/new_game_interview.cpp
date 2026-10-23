#include "new_game_interview.hpp"

#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/content_id.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/math.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/render/camera.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/map_head_lines.hpp>

#include <glm/ext/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

#include "story_text_rows.hpp"

namespace dwe::app {

namespace {

// The lines of the scene's table (newGameStateMachine: resolveMapHeadEntry(0x4D6, i)).
constexpr std::size_t Greeting = 0x10;
constexpr std::size_t AskDigivice = 0x11;
constexpr std::size_t YesNo = 0x12;
constexpr std::size_t ISeeYes = 0x13;
constexpr std::size_t AskDayNight = 0x14;
constexpr std::size_t DayNight = 0x15;
constexpr std::size_t ISeeNo = 0x16;
constexpr std::size_t AskFriends = 0x17;
constexpr std::size_t FriendsYesNo = 0x18;
constexpr std::size_t Alright = 0x19;
constexpr std::size_t AskName = 0x1A;
constexpr std::size_t NameRight = 0x1B;
constexpr std::size_t RightYesNo = 0x1C;
constexpr std::size_t AskPartnerName = 0x1D;
constexpr std::size_t PartnerNameRight = 0x1E;
constexpr std::size_t GoodName = 0x1F;
constexpr std::size_t ThatsAll = 0x20;

// The states of newGameStateMachine that do something else than show a line.
constexpr std::uint8_t AnswerFirst = 0x08;
constexpr std::uint8_t AnswerSecond = 0x09;
constexpr std::uint8_t CloseForName = 0x13;
constexpr std::uint8_t OpenNaming = 0x14;
constexpr std::uint8_t StoreName = 0x15;
constexpr std::uint8_t ConfirmName = 0x16;
constexpr std::uint8_t NameTheTamerAgain = 0x19;
constexpr std::uint8_t NameThePartner = 0x1A;
constexpr std::uint8_t NameThePartnerAgain = 0x1D;
constexpr std::uint8_t Done = 0x1F;

// The original's screen is 240 lines tall; a projection distance over half of it gives the field
// of view.
constexpr float ScreenHalfLines = 120.0F;
constexpr float NearPlane = 10.0F;
constexpr float FarPlane = 20000.0F;
constexpr std::string_view SpeciesModelFolder = "digimon/";
constexpr std::string_view SpeciesModelName = "/model";
constexpr std::string_view TamerArgument = "tamer";
// new_game.c: loadNewGameScene: three flat lights (where they travel, in the original's axes, and
// 8-bit colours where 256 is full) and the ambient 0x800, half of the GTE's 4096; as the field's
// map lights (field_scene.cpp: mapLighting).
render::ModelLighting sceneLighting() {
  constexpr float FullColor = 256.0F;
  constexpr float Ambient = 0.5F;
  constexpr float Side = 30.0F;
  constexpr float Down = 100.0F;
  constexpr float First = 64.0F;
  constexpr float Second = 40.0F;
  constexpr float Third = 38.0F;
  const auto light = [](glm::vec3 direction, float color) {
    return render::DirectionalLight{.direction = core::fromOriginalAxes(direction),
                                    .color = glm::vec3{color / FullColor}};
  };
  return {.lights = {light({Side, Down, Side}, First), light({-Side, Down, 0.0F}, Second),
                     light({0.0F, Down, -Side}, Third)},
          .ambient = glm::vec3{Ambient}};
}
constexpr std::string_view PartnerArgument = "partner";

} // namespace

core::Result<std::unique_ptr<NewGameInterview>>
NewGameInterview::create(render::Renderer& renderer, ui::Ui& ui,
                         const assets::VirtualFileSystem& files, NewGameSceneRules rules,
                         game::story::SoundPort* sound) {
  auto tables = session::readMapHeadLineTables(files);
  if (!tables) {
    return std::unexpected{tables.error()};
  }
  const auto table = tables->find(rules.lines);
  if (table == tables->end() || table->second.size() <= ThatsAll) {
    return core::fail(std::format("new game scene: MAPHEAD has no line table {} with the "
                                  "opening's lines",
                                  rules.lines));
  }
  auto models = render::ModelRenderer::create(renderer);
  if (!models) {
    return std::unexpected{models.error()};
  }
  std::string modelName{SpeciesModelFolder};
  modelName.append(assets::contentAssetPath(rules.host.species)).append(SpeciesModelName);
  auto id = assets::AssetId::parse(modelName);
  if (!id) {
    return std::unexpected{id.error()};
  }
  auto model = assets::loadModel(files, *id);
  if (!model) {
    return std::unexpected{model.error()};
  }
  std::unique_ptr<NewGameInterview> interview{
      new NewGameInterview(ui, std::move(rules), std::move(table->second), std::move(*models),
                           std::make_unique<assets::ModelData>(std::move(*model)))};
  auto handle = interview->models_->add(*interview->host_);
  if (!handle) {
    return std::unexpected{handle.error()};
  }
  interview->hostHandle_ = *handle;
  interview->sound_ = sound;
  interview->naming_.setSound(sound);
  return interview;
}

NewGameInterview::NewGameInterview(ui::Ui& ui, NewGameSceneRules rules,
                                   std::vector<std::string> lines,
                                   std::unique_ptr<render::ModelRenderer> models,
                                   std::unique_ptr<assets::ModelData> host)
    : ui_(&ui), rules_(std::move(rules)), lines_(std::move(lines)), models_(std::move(models)),
      host_(std::move(host)), hostPlayer_(std::make_unique<animation::AnimationPlayer>(*host_)),
      hostPosition_(core::fromOriginalAxes(rules_.host.position)),
      hostYaw_(assets::yawFromOriginal(rules_.host.yaw)),
      naming_(ui, rules_.naming, rules_.sounds) {
  drawers_ = {&models_->opaque(), &models_->translucent()};
  models_->setLighting(sceneLighting());
  // new_game.c: loadNewGameScene: the host walks in with its first clip, turned away.
  if (!hostPlayer_->play(rules_.host.clip)) {
    core::logWarning("new game scene: {} has no clip {}", rules_.host.species, rules_.host.clip);
  }
  core::logDebug("[port:story] loadNewGameScene (new_game.c:44): {} at ({}, {}, {}), rotation {}, "
                 "clip {}",
                 rules_.host.species, rules_.host.position.x, rules_.host.position.y,
                 rules_.host.position.z, rules_.host.yaw, rules_.host.clip);
}

NewGameInterview::~NewGameInterview() {
  naming_.close();
  showText(false);
}

std::uint64_t NewGameInterview::ticksOf(std::uint32_t frames) const {
  return static_cast<std::uint64_t>(frames) * rules_.frameTicks;
}

void NewGameInterview::tick(const input::InputState& input) {
  ++ticks_;
  if (!turned_ && ticks_ >= ticksOf(rules_.host.turnFrame)) {
    turnHost();
  }
  if (!turned_ || endTick_) {
    return;
  }
  take(input);
  step();
}

void NewGameInterview::turnHost() {
  // new_game.c: tickNewGameJijimon: on frame 0x23 the host faces the player and the questions
  // start (pstat 0xF3 = 0).
  turned_ = true;
  hostYaw_ = assets::yawFromOriginal(rules_.host.turnedYaw);
  static_cast<void>(hostPlayer_->play(rules_.host.turnedClip));
  core::logDebug("[port:story] tickNewGameJijimon (new_game.c:33): frame {}, rotation {}, clip {}; "
                 "the questions start",
                 rules_.host.turnFrame, rules_.host.turnedYaw, rules_.host.turnedClip);
}

void NewGameInterview::play(const game::story::DialogueSound& sound) {
  if (sound_ != nullptr) {
    sound_->playSound(sound.slot, sound.sound);
  }
}

void NewGameInterview::take(const input::InputState& input) {
  // script_textbox.c: flipIdleTextboxPage: no press counts while the page is still being drawn,
  // one row a frame.
  if (ticks_ < inputFrom_) {
    return;
  }
  switch (waiting_) {
  case Waiting::Nothing:
    return;
  case Waiting::Line:
    // script_textbox.c: tickConfirmDialogue: confirm closes the page (playSound(0, 3)).
    if (input.wasPressed("confirm")) {
      play(rules_.sounds.confirm);
      waiting_ = Waiting::Nothing;
      state_ = next_;
    }
    return;
  case Waiting::Selection: {
    // script_textbox.c: the selection's cursor moves over the box's rows; confirm picks the row and
    // the state machine goes on to the next state plus the row (nextState + choice).
    constexpr std::size_t Choices = 2;
    // tickSelectionDialogue: the cursor wraps and moves with playSound(0, 2); confirm plays 3; the
    // scene keeps trigger 49 set, so cancel is refused (playSound(0, 0xB)).
    if (input.wasPressed("move_up")) {
      cursor_ = (cursor_ + Choices - 1) % Choices;
      play(rules_.sounds.cursor);
      showText(true);
    } else if (input.wasPressed("move_down")) {
      cursor_ = (cursor_ + 1) % Choices;
      play(rules_.sounds.cursor);
      showText(true);
    } else if (input.wasPressed("cancel")) {
      play(rules_.sounds.refused);
    } else if (input.wasPressed("confirm")) {
      play(rules_.sounds.confirm);
      waiting_ = Waiting::Nothing;
      state_ = static_cast<std::uint8_t>(next_ + cursor_);
      core::logDebug(
          "[port:story] newGameStateMachine (script_menu.c:1144): choice {} → state {:#x}", cursor_,
          state_);
    }
    return;
  }
  case Waiting::Naming:
    if (naming_.press(input)) {
      waiting_ = Waiting::Nothing;
      state_ = StoreName;
    }
    return;
  }
}

void NewGameInterview::step() {
  if (waiting_ != Waiting::Nothing || endTick_) {
    return;
  }
  // shop/script_menu.c: newGameStateMachine: a state shows a line and waits for it, or acts.
  switch (state_) {
  case 0x00:
    say(Greeting, 0x02);
    return;
  case 0x02:
    say(AskDigivice, 0x03);
    return;
  case 0x03:
    ask(YesNo, 0x04);
    return;
  case 0x04:
    say(ISeeYes, 0x06);
    return;
  case 0x06:
    say(AskDayNight, 0x07);
    return;
  case 0x07:
    ask(DayNight, AnswerFirst);
    return;
  case 0x05:
    say(ISeeNo, 0x0A);
    return;
  case 0x0A:
    say(AskFriends, 0x0B);
    return;
  case 0x0B:
    ask(FriendsYesNo, AnswerFirst);
    return;
  case AnswerFirst:
  case AnswerSecond:
    // pstat 0xFE: the answer that picks the partner (main.c: main).
    answer_ = static_cast<std::size_t>(state_ - AnswerFirst);
    core::logDebug("[port:story] newGameStateMachine (script_menu.c:1144): answer {} (pstat 0xFE)",
                   *answer_);
    state_ = 0x11;
    step();
    return;
  case 0x11:
    say(Alright, 0x12);
    return;
  case 0x12:
    say(AskName, CloseForName);
    return;
  case CloseForName:
    showText(false);
    state_ = OpenNaming;
    step();
    return;
  case OpenNaming:
    // initializeNamingBuffer: the box starts from the name so far (none yet for the partner).
    naming_.open(target_, target_ == NameTarget::Tamer ? tamerName() : partnerName_);
    waiting_ = Waiting::Naming;
    return;
  case StoreName:
    (target_ == NameTarget::Tamer ? tamerName_ : partnerName_) = naming_.name();
    naming_.close();
    state_ = ConfirmName;
    step();
    return;
  case ConfirmName:
    if (target_ == NameTarget::Tamer) {
      say(NameRight, 0x17);
    } else {
      say(PartnerNameRight, 0x1B);
    }
    return;
  case 0x17:
    ask(RightYesNo, 0x18);
    return;
  case 0x18:
    say(AskPartnerName, NameThePartner);
    return;
  case NameThePartner:
    target_ = NameTarget::Partner;
    partnerName_.clear();
    state_ = CloseForName;
    step();
    return;
  case NameTheTamerAgain:
  case NameThePartnerAgain:
    state_ = CloseForName;
    step();
    return;
  case 0x1B:
    ask(RightYesNo, 0x1C);
    return;
  case 0x1C:
    say(GoodName, 0x1E);
    return;
  case 0x1E:
    say(ThatsAll, Done);
    return;
  case Done:
    // The box closes and the scene fades out (main.c: newGameScene: fadeToBlack(0x28)).
    showText(false);
    endTick_ = ticks_;
    core::logDebug(
        "[port:story] newGameScene (main.c:3767): the questions are over; '{}' and '{}', "
        "answer {}; fadeToBlack({})",
        tamerName(), partnerName_, answer_.value_or(0), rules_.fadeOutFrames);
    return;
  default:
    core::logWarning("new game scene: no state {:#x}", state_);
    state_ = Done;
    step();
    return;
  }
}

void NewGameInterview::say(std::size_t line, std::uint8_t next) {
  shownLine_ = line;
  next_ = next;
  waiting_ = Waiting::Line;
  showText(true);
}

void NewGameInterview::ask(std::size_t line, std::uint8_t next) {
  shownLine_ = line;
  next_ = next;
  cursor_ = 0;
  waiting_ = Waiting::Selection;
  showText(true);
}

void NewGameInterview::showText(bool open) {
  if (!open) {
    if (shownLine_) {
      shownLine_.reset();
      screens::publish(*ui_, textboxSettings(TextboxLook{}, {}), TextboxRevisionSetting);
      ui_->hide(StoryTextboxDocument);
    }
    return;
  }
  if (!shownLine_) {
    return;
  }
  std::vector<TextRow> rows;
  appendPageRows(rows, lineText(*shownLine_));
  inputFrom_ = ticks_ + ticksOf(static_cast<std::uint32_t>(rows.size()));
  // The selection's rows come after the speaker's name.
  const TextboxLook look{.open = true,
                         .speech = true,
                         .columns = 0,
                         .rows = 0,
                         .opening = 1.0,
                         .origin = std::nullopt,
                         .prompt = waiting_ == Waiting::Line,
                         .cursorRow = waiting_ == Waiting::Selection
                                          ? std::optional<std::size_t>{1 + cursor_}
                                          : std::nullopt};
  screens::publish(*ui_, textboxSettings(look, rows), TextboxRevisionSetting);
  ui_->show(StoryTextboxDocument);
}

std::string NewGameInterview::lineText(std::size_t line) const {
  std::vector<localization::TextArgument> arguments = colorArguments();
  arguments.push_back({.name = TamerArgument, .value = tamerName()});
  arguments.push_back({.name = PartnerArgument, .value = partnerName_});
  return localization::tr(lines_.at(line), arguments);
}

std::string NewGameInterview::tamerName() const {
  // The American version starts the player's name empty (newGameStateMachine: MAIN_D_80134600[7]).
  return tamerName_;
}

void NewGameInterview::update(double frameSeconds, rhi::Extent extent) {
  // The host walks by its clip's root motion, turned the way it faces.
  const glm::vec3 step = hostPlayer_->advance(static_cast<float>(frameSeconds));
  hostPosition_ += glm::vec3{glm::rotate(glm::mat4{1.0F}, hostYaw_, glm::vec3{0.0F, 1.0F, 0.0F}) *
                             glm::vec4{step, 0.0F}};
  // new_game.c: loadNewGameScene: GsSetRefView2 from (0, 0, -0x1068) to the origin, the projection
  // 0x3E8 and the drawing offset's line as the projection's centre.
  const glm::vec3 eye = core::fromOriginalAxes(glm::vec3{0.0F, 0.0F, -rules_.cameraDistance});
  const glm::mat4 view = glm::lookAt(eye, glm::vec3{0.0F}, glm::vec3{0.0F, 1.0F, 0.0F});
  const float aspect = extent.height > 0
                           ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                           : 1.0F;
  const float fovY = 2.0F * std::atan(ScreenHalfLines / rules_.cameraProjection);
  // Vulkan's clip space has Y down: a centre below the middle moves the picture down.
  const float centre = (rules_.cameraCentreLine - ScreenHalfLines) / ScreenHalfLines;
  const glm::mat4 projection = glm::translate(glm::mat4{1.0F}, glm::vec3{0.0F, centre, 0.0F}) *
                               render::perspective(fovY, aspect, NearPlane, FarPlane);
  models_->setCamera({.view = view, .projection = projection, .viewport = {}});
  models_->clear();
  const glm::mat4 world = glm::rotate(glm::translate(glm::mat4{1.0F}, hostPosition_), hostYaw_,
                                      glm::vec3{0.0F, 1.0F, 0.0F});
  models_->submit({.model = hostHandle_, .world = world, .joints = hostPlayer_->jointMatrices()});
}

float NewGameInterview::fadeLevel() const {
  if (endTick_) {
    const std::uint64_t length = ticksOf(rules_.fadeOutFrames);
    return length == 0 ? 1.0F
                       : std::min(1.0F, static_cast<float>(ticks_ - *endTick_) /
                                            static_cast<float>(length));
  }
  const std::uint64_t length = ticksOf(rules_.fadeInFrames);
  return length == 0 || ticks_ >= length
             ? 0.0F
             : 1.0F - (static_cast<float>(ticks_) / static_cast<float>(length));
}

bool NewGameInterview::finished() const {
  return endTick_ && ticks_ - *endTick_ >= ticksOf(rules_.fadeOutFrames);
}

session::NewGameChoice NewGameInterview::choice() const {
  const std::size_t answer = std::min(answer_.value_or(0), rules_.partners.size() - 1);
  return {
      .partner = rules_.partners.at(answer), .tamerName = tamerName_, .partnerName = partnerName_};
}

} // namespace dwe::app
