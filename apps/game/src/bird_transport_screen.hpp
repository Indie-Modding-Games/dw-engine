#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/session/bird_transport.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/session/story_scenes.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "field_scene.hpp"

namespace dwe::app {

inline constexpr std::string_view BirdTransportDocument = "bird_transport";

// The transport menu's cursor (ItemMenuBox of tickBirdraTransport): six rows that never scroll;
// the cursor stops at the ends (itemMenuCursorUp and itemMenuCursorDown).
struct BirdCursor {
  std::int32_t row = 0;

  void up() { row = row > 0 ? row - 1 : 0; }
  void down(std::int32_t count) { row = row + 1 < count ? row + 1 : row; }
};

struct BirdTransportScreenDesc {
  // They must outlive the screen; without a transport the story's routine 10 closes at once.
  session::BirdTransport* transport = nullptr;
  // The player's name for the yes-or-no; the refusal's sound.
  const session::GameSession* session = nullptr;
  game::story::SoundPort* sound = nullptr;
  ui::Ui* ui = nullptr;
  // The story lock: a place on a map it keeps closed is red and cannot be picked. Without it every
  // place is open.
  const session::StoryAdapter* story = nullptr;
};

// The bird transport the story opens (call_routine 10 -> shop/script_menu.c: tickBirdraTransport,
// createBirdraTransportMenu, tickBirdraTransportMenu, renderBirdraTransportMenu,
// calculateBirdramonMenuStrings), on the bird_transport document: the carrier asks where to, the
// money box and the list of places with their price come up, the player picks one he can pay
// for, the carrier asks again and the player says yes or no. A place the story lock keeps closed is
// red like one the money does not pay for. The yes pays and the story goes on
// at the MAPHEAD section that flies him there; the no or cancel get the farewell. The field and
// the game time wait meanwhile.
class BirdTransportScreen {
public:
  explicit BirdTransportScreen(BirdTransportScreenDesc desc);

  void setSpecies(const assets::DigimonTable* species) { species_ = species; }
  // Whether a visit is open: the field, the game time and the game's menu wait meanwhile.
  [[nodiscard]] bool active() const;
  // Takes the story's BirdTransport screen, with the carrier the operand (variable 254) names on
  // the map; false for any other screen.
  bool take(const session::StoryScreenRequest& request, const FieldScene& scene);
  // Takes the document's commands while a visit is open; false for any other command.
  bool handle(const ui::UiCommand& command);
  // Once per engine tick: R1 (walk) held makes up and down jump to the ends (itemMenuCursorTop,
  // itemMenuCursorBottom).
  void press(const input::InputState& input);
  // Once per frame: publishes the visit, and tells the story once it closed.
  void update(session::StoryScenes& scenes);

  // The visit's steps (tickBirdraTransport's states), for the tests and the log.
  enum class Phase : std::uint8_t {
    Closed,
    Greeting, // state 0: line 4
    List,     // state 3: the money box and the menu, line 4 under them
    Confirm,  // tickBirdraTransportMenu's confirm: line 6
    Choice,   // state 4: the yes-or-no
    Farewell, // state 6: line 5, the boxes closed
  };
  [[nodiscard]] Phase phase() const { return phase_; }

private:
  void act(std::string_view action);
  void actInList(std::string_view action);
  void refuse();
  void close(std::optional<std::int32_t> travelSection);
  [[nodiscard]] std::int32_t rowCount() const;
  [[nodiscard]] std::string playerName() const;
  [[nodiscard]] screens::ScreenSettings settings() const;

  BirdTransportScreenDesc desc_;
  const assets::DigimonTable* species_ = nullptr;
  // The names of the places (the map table), taken from the scene on take.
  const assets::MapTable* maps_ = nullptr;
  std::string carrierSpecies_;
  Phase phase_ = Phase::Closed;
  BirdCursor cursor_;
  // DIALOGUE_SELECTION.current: 0 yes, 1 no.
  std::int32_t choice_ = 0;
  bool jumpHeld_ = false;
  // Whether the story waits for the visit to close, and the MAPHEAD section it goes on at.
  bool waiting_ = false;
  std::optional<std::int32_t> travelSection_;
};

} // namespace dwe::app
