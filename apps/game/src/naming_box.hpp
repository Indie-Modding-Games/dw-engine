#pragma once

#include <dwe/input/input_state.hpp>
#include <dwe/ui/ui.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "new_game_scene_rules.hpp"

namespace dwe::app {

inline constexpr std::string_view NamingBoxDocument = "naming_box";

// Whose name the box asks for (script_menu.c: NAMING_BOX_FLAG bit 0).
enum class NameTarget : std::uint8_t { Tamer, Partner };

// The naming box (script_menu.c: setupNameSelectorBox, setupNameDisplayBox, tickNamingBox): a grid
// of letters with Back and OK below it and the name above, at most `length` letters. Confirm writes
// the letter under the cursor (the last one again once the name is full, and the cursor goes to
// OK); cancel takes the last letter back; menu goes to OK; OK takes a name that is not empty. It
// publishes naming_box.* settings and takes the field's buttons, not the UI's focus.
class NamingBox {
public:
  NamingBox(ui::Ui& ui, NamingRules rules);
  ~NamingBox();
  NamingBox(const NamingBox&) = delete;
  NamingBox& operator=(const NamingBox&) = delete;
  NamingBox(NamingBox&&) = delete;
  NamingBox& operator=(NamingBox&&) = delete;

  // Opens the box with the name so far (initializeNamingBuffer copies the current name).
  void open(NameTarget target, std::string_view name);
  void close();
  [[nodiscard]] bool isOpen() const { return open_; }
  // One engine tick of the player's buttons; true once OK took the name.
  bool press(const input::InputState& input);
  // The name typed, as one text.
  [[nodiscard]] std::string name() const;

private:
  // A cell of the grid: rows of 2 * columns letters (the left and the right side), then the row of
  // the buttons.
  [[nodiscard]] std::size_t gridRows() const;
  [[nodiscard]] std::size_t gridColumns() const;
  [[nodiscard]] std::string letterAt(std::size_t row, std::size_t column) const;
  [[nodiscard]] bool onButtons() const { return row_ == gridRows(); }
  [[nodiscard]] bool onOk() const { return onButtons() && column_ >= rules_.columns; }
  void move(int rows, int columns);
  void write();
  void goToOk();
  void publish();

  ui::Ui* ui_;
  NamingRules rules_;
  NameTarget target_ = NameTarget::Tamer;
  std::vector<std::string> letters_;
  std::size_t row_ = 0;
  std::size_t column_ = 0;
  bool open_ = false;
};

} // namespace dwe::app
