#include "egg_box_screen.hpp"

#include <dwe/core/log.hpp>

#include <variant>

namespace dwe::app {

namespace {

constexpr std::string_view EggsSetting = "egg_box_eggs";
constexpr std::string_view MoveCommand = "egg_box_move";
constexpr std::string_view ChooseCommand = "egg_box_choose";

} // namespace

EggBoxScreen::EggBoxScreen(session::GameSession& session, ui::Ui& ui)
    : session_{&session}, ui_{&ui} {}

bool EggBoxScreen::take(const session::StoryScreenRequest& request,
                        const session::StoryScenes& scenes) {
  if (request.screen != session::StoryScreen::EggBox) {
    return false;
  }
  sounds_.reset();
  if (const auto& death = scenes.rules().death; death.has_value()) {
    sounds_ = death->eggBox;
  }
  eggs_ = session_->partnerRules().life.rebirth.babies.size();
  chosen_.reset();
  if (eggs_ == 0 || !request.waits) {
    core::logWarning("[port:death] DOO2_openEggBox (doo2.c:721): no egg to take, the box closes");
    over_ = request.waits;
    return true;
  }
  core::logDebug("[port:death] DOO2_openEggBox (doo2.c:721): {} eggs", eggs_);
  ui_->setSetting(EggsSetting, static_cast<double>(eggs_));
  ui_->show(EggBoxDocument, true);
  open_ = true;
  playSound(sounds_.transform([](const session::StoryEggBoxRules& box) { return box.open; }));
  return true;
}

bool EggBoxScreen::handle(const ui::UiCommand& command) {
  if (!open_ || (command.name != MoveCommand && command.name != ChooseCommand)) {
    return false;
  }
  const double* egg = std::get_if<double>(&command.value);
  if (command.name == MoveCommand) {
    playSound(sounds_.transform([](const session::StoryEggBoxRules& box) { return box.move; }));
    return true;
  }
  if (egg == nullptr || *egg < 0.0 || static_cast<std::size_t>(*egg) >= eggs_) {
    core::logWarning("[port:death] DOO2_tickEggInput (doo2.c:739): no such egg");
    return true;
  }
  chosen_ = static_cast<std::size_t>(*egg);
  playSound(sounds_.transform([](const session::StoryEggBoxRules& box) { return box.choose; }));
  ui_->hide(EggBoxDocument);
  open_ = false;
  over_ = true;
  return true;
}

void EggBoxScreen::update(session::StoryScenes& scenes) {
  if (!over_) {
    return;
  }
  over_ = false;
  if (chosen_.has_value()) {
    scenes.chooseEgg(*chosen_);
  }
  chosen_.reset();
  scenes.screenClosed();
}

void EggBoxScreen::playSound(const std::optional<session::StorySceneSound>& sound) const {
  game::story::SoundPort* port = session_->sound();
  if (port != nullptr && sound.has_value()) {
    port->playSound(sound->slot, sound->sound);
  }
}

} // namespace dwe::app
