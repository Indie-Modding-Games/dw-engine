#pragma once

#include <dwe/game/story/story_ports.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/ui/ui.hpp>

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "new_game_scene_rules.hpp"

namespace dwe::app {

inline constexpr std::string_view NamingBoxDocument = "naming_box";

// The longest name with the "Long names" option (an engine improvement, docs/modulos/apps/game.md):
// twelve letters, as many as the original's name box holds at its 12-pixel spacing (149 pixels
// wide); without the option the ruleset's length, six letters in the original.
inline constexpr std::size_t LongNameLength = 12;

// The letters a name may have under `rules`, with or without the "Long names" option.
[[nodiscard]] inline std::size_t nameLength(const NamingRules& rules, bool longNames) {
  return longNames && rules.length < LongNameLength ? LongNameLength : rules.length;
}

// Whose name the box asks for (script_menu.c: NAMING_BOX_FLAG bit 0).
enum class NameTarget : std::uint8_t { Tamer, Partner };

// The naming box (script_menu.c: setupNameSelectorBox, setupNameDisplayBox, tickNamingBox): two
// sides of letters, `columns` wide, with Back and OK beside them and the name above, at most
// `length` letters. Confirm writes the letter under the cursor (the last one again once the name is
// full, and the cursor goes to OK); cancel takes the last letter back; alt goes to Back and menu to
// OK; extra clears the whole name; OK takes a name that is not empty. The movement keys repeat
// while held. The keyboard can also type the name: typed letters of the grid (and spaces) are
// written, and Return takes the name once something was typed. The pointer (the engine's, the
// original has none) moves the cursor onto the cell or button it moves over, silently; a click on
// a cell writes it and on Back or OK does what confirm there does; a click elsewhere does nothing.
// It publishes naming_box.* settings and takes the field's buttons, not the UI's focus.
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

  // The "Long names" option: names of up to LongNameLength letters, or the ruleset's length. It
  // applies from the next open().
  void setLongNames(bool on);
  [[nodiscard]] std::size_t length() const { return length_; }

  // Opens the box with the name so far (initializeNamingBuffer copies the current name).
  void open(NameTarget target, std::string_view name);
  void close();
  [[nodiscard]] bool isOpen() const { return open_; }
  // What the keyboard typed since the last tick (UTF-8), and whether Return was pressed; the next
  // press() takes it.
  void type(std::string_view text, bool enter);
  // The UI API's naming commands (story_commands.hpp: naming_write, naming_erase, naming_accept),
  // which the next press() takes before the buttons: the letters are written as typed ones (a
  // letter the grid lacks is refused), then the erases, then OK. False for another command or with
  // the box closed.
  bool handle(const ui::UiCommand& command);
  // One engine tick of the player's buttons; true once OK took the name.
  bool press(const input::InputState& input);
  // The name typed, as one text.
  [[nodiscard]] std::string name() const;

private:
  enum class Button : std::uint8_t { Back, Ok };
  // A place of the cursor: a button, or a cell by side, row and column.
  struct Spot {
    std::optional<Button> button;
    std::size_t side = 0;
    std::size_t row = 0;
    std::size_t column = 0;
  };

  [[nodiscard]] std::string letterAt(std::size_t side, std::size_t row, std::size_t column) const;
  [[nodiscard]] bool knows(std::string_view letter) const;
  [[nodiscard]] std::size_t sideRows() const { return rules_.rows.size() / 2; }
  bool takeTyping();
  bool takeAsked();
  bool pressButtons(const input::InputState& input);
  // The cell or button under a window point, as the document was last laid out.
  [[nodiscard]] std::optional<Spot> spotAt(glm::vec2 point) const;
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
  // The letters a name may have: the ruleset's, or LongNameLength with the option.
  std::size_t length_ = 0;
  // The length open() last took, which the box keeps while it is open.
  std::size_t openLength_ = 0;
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
  // What the UI's naming commands asked since the last tick.
  struct Asked {
    std::string text;
    std::size_t erases = 0;
    bool accept = false;
  };
  std::optional<Asked> asked_;
};

} // namespace dwe::app
