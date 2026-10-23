#pragma once

#include <dwe/session/game_session.hpp>
#include <dwe/session/story_scenes.hpp>
#include <dwe/ui/ui.hpp>

#include <cstddef>
#include <optional>
#include <string_view>

namespace dwe::app {

inline constexpr std::string_view EggBoxDocument = "egg_box";

// The egg box after a death (doo2.c: DOO2_openEggBox, DOO2_tickEggInput): the player takes the egg
// the partner is reborn from, one per baby of the rebirth, and the story goes on once one is taken.
// The box's animation and the eggs' sprites are not drawn: the document lists the eggs.
class EggBoxScreen {
public:
  // The session and the UI must outlive the screen.
  EggBoxScreen(session::GameSession& session, ui::Ui& ui);

  // Takes the story's EggBox screen and opens the box; false for any other screen.
  bool take(const session::StoryScreenRequest& request, const session::StoryScenes& scenes);
  // The document's commands: a move plays its sound, a choice closes the box.
  bool handle(const ui::UiCommand& command);
  // Tells the story the egg taken and closes its screen, once.
  void update(session::StoryScenes& scenes);

private:
  void playSound(const std::optional<session::StorySceneSound>& sound) const;

  session::GameSession* session_;
  ui::Ui* ui_;
  std::optional<session::StoryEggBoxRules> sounds_;
  std::size_t eggs_ = 0;
  bool open_ = false;
  bool over_ = false;
  std::optional<std::size_t> chosen_;
};

} // namespace dwe::app
