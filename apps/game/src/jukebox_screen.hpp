#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/jukebox.hpp>
#include <dwe/session/story_scenes.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace dwe::app {

class FieldScene;

inline constexpr std::string_view JukeboxDocument = "jukebox";

// openJukebox's list (allocateItemMenuBox(..., 6, ...)): six rows shown at a time.
inline constexpr std::int32_t JukeboxVisibleRows = 6;

struct JukeboxScreenDesc {
  // They must outlive the screen; without a jukebox or music the story's routine 9 closes at once.
  session::Jukebox* jukebox = nullptr;
  session::GameSession* session = nullptr;
  const assets::DigimonTable* species = nullptr;
  ui::Ui* ui = nullptr;
};

// The jukebox the story opens (call_routine 9 -> shop/script_menu.c: openJukebox,
// createJukeboxMenu, tickJukeboxMenu), on the jukebox document: the owner's greeting, the list of
// songs with the one that plays marked and the owner's question over it, the owner's answer to a
// pick, whose song then plays, and the farewell, after which the map's song plays again. The field
// and the game time wait meanwhile.
class JukeboxScreen {
public:
  explicit JukeboxScreen(JukeboxScreenDesc desc);

  [[nodiscard]] bool active() const;
  // Takes the story's Jukebox screen; false for any other screen. The owner is the map's NPC the
  // routine's variable names.
  bool take(const session::StoryScreenRequest& request, const FieldScene& scene);
  // Takes the document's commands while the jukebox is open; false for any other command.
  bool handle(const ui::UiCommand& command);
  // Once per engine tick: R1 (walk) held makes up and down jump to the ends.
  void press(const input::InputState& input);
  // Once per frame: publishes the jukebox, and tells the story once it closed.
  void update(session::StoryScenes& scenes);

private:
  enum class Phase : std::uint8_t { Closed, Greeting, List, Farewell };

  void act(std::string_view action);
  void actInList(std::string_view action);
  void close();
  [[nodiscard]] screens::ScreenSettings settings() const;

  JukeboxScreenDesc desc_;
  Phase phase_ = Phase::Closed;
  session::JukeboxListView list_;
  // The owner's species, for its name over the lines.
  std::string owner_;
  // Whether the owner answered a pick ("<song>, right?") over the list.
  bool answered_ = false;
  bool jumpHeld_ = false;
  // Whether the story waits for the jukebox to close.
  bool waiting_ = false;
};

} // namespace dwe::app
