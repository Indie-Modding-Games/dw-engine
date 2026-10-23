#pragma once

#include <dwe/game/story/story_ports.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/ui/ui.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "new_game_scene_rules.hpp"

namespace dwe::app {

inline constexpr std::string_view NamingBoxDocument = "naming_box";

// Whose name the box asks for (script_menu.c: NAMING_BOX_FLAG bit 0).
enum class NameTarget : std::uint8_t { Tamer, Partner };

// The naming box (script_menu.c: setupNameSelectorBox, setupNameDisplayBox, tickNamingBox): two
// sides of letters, `columns` wide, with Back and OK beside them and the name above, at most
// `length` letters. Confirm writes the letter under the cursor (the last one again once the name is
// full, and the cursor goes to OK); cancel takes the last letter back; alt goes to Back and menu to
// OK; extra clears the whole name; OK takes a name that is not empty. The movement keys repeat
// while held. The keyboard can also type the name: typed letters of the grid (and spaces) are
// written, and Return takes the name once something was typed. It publishes naming_box.* settings
// and takes the field's buttons, not the UI's focus.
class NamingBox {
public:
  NamingBox(ui::Ui& ui, NamingRules rules, SceneSounds sounds);
  ~NamingBox();
  NamingBox(const NamingBox&) = delete;
  NamingBox& operator=(const NamingBox&) = delete;
  NamingBox(NamingBox&&) = delete;
  NamingBox& operator=(NamingBox&&) = delete;

  // Where the box's sounds play (playSound); none plays without it.
  void setSound(game::story::SoundPort* sound) { sound_ = sound; }

  // Opens the box with the name so far (initializeNamingBuffer copies the current name).
  void open(NameTarget target, std::string_view name);
  void close();
  [[nodiscard]] bool isOpen() const { return open_; }
  // What the keyboard typed since the last tick (UTF-8), and whether Return was pressed; the next
  // press() takes it.
  void type(std::string_view text, bool enter);
  // One engine tick of the player's buttons; true once OK took the name.
  bool press(const input::InputState& input);
  // The name typed, as one text.
  [[nodiscard]] std::string name() const;

private:
  enum class Button : std::uint8_t { Back, Ok };

  [[nodiscard]] std::string letterAt(std::size_t side, std::size_t row, std::size_t column) const;
  [[nodiscard]] bool knows(std::string_view letter) const;
  [[nodiscard]] std::size_t sideRows() const { return rules_.rows.size() / 2; }
  bool takeTyping();
  bool pressButtons(const input::InputState& input);
  void repeatMoves(const input::InputState& input);
  void move(std::string_view action);
  void write(const std::string& letter);
  void deleteLast();
  bool accept();
  void goTo(Button button);
  void play(const game::story::DialogueSound& sound);
  void publish();

  ui::Ui* ui_;
  NamingRules rules_;
  SceneSounds sounds_;
  game::story::SoundPort* sound_ = nullptr;
  NameTarget target_ = NameTarget::Tamer;
  std::vector<std::string> letters_;
  std::size_t side_ = 0;
  std::size_t row_ = 0;
  std::size_t column_ = 0;
  std::optional<Button> button_;
  // The movement key held and for how many ticks (setInputRepeatMask(0xF000)).
  std::string held_;
  std::uint64_t heldTicks_ = 0;
  std::string typed_;
  bool enter_ = false;
  // Whether the keyboard typed since the cursor last moved: Return then takes the name.
  bool typing_ = false;
  bool open_ = false;
};

} // namespace dwe::app
