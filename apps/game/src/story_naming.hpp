#pragma once

#include <dwe/input/input_state.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_scenes.hpp>

#include <cstdint>

#include "naming_box.hpp"

namespace dwe::app {

// The story's renaming (call_routine 32 with pstat 254 at 2 or 3: script_menu.c:
// initializeNamingBuffer with bit 1 set): the naming box alone, for the tamer's name (bit 0 clear)
// or the partner's, and the story goes on once OK takes the name.
class StoryNaming {
public:
  // Both must outlive this object; without a box the story's renaming ends at once.
  void attach(NamingBox* box, session::GameSession* session) {
    box_ = box;
    session_ = session;
  }
  // Takes the story's NewGameMenu screen; false for any other screen.
  bool take(const session::StoryScreenRequest& request);
  // One engine tick of the player's buttons while the box is open; true when it took them.
  bool press(const input::InputState& input);
  // Tells the story that the name is given.
  void update(session::StoryScenes& scenes);

private:
  NamingBox* box_ = nullptr;
  session::GameSession* session_ = nullptr;
  NameTarget target_ = NameTarget::Tamer;
  bool waiting_ = false;
  bool done_ = false;
};

} // namespace dwe::app
