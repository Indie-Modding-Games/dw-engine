#include "naming_box.hpp"

#include <dwe/core/log.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <format>
#include <utility>

namespace dwe::app {

namespace {

constexpr std::string_view Prefix = "naming_box";
constexpr std::string_view RevisionSetting = "naming_box.revision";
constexpr std::string_view TamerTitle = "naming.tamer";
constexpr std::string_view PartnerTitle = "naming.partner";
// A space is an empty cell: it is no letter to start a name with (tickNamingBox: 0x8140 first).
constexpr std::string_view Blank = " ";
constexpr unsigned char ContinuationMask = 0xC0U;
constexpr unsigned char ContinuationBits = 0x80U;

// The letters of a row: one UTF-8 character each.
std::vector<std::string> lettersOf(std::string_view row) {
  std::vector<std::string> letters;
  for (const char c : row) {
    const auto byte = static_cast<unsigned char>(c);
    if ((byte & ContinuationMask) == ContinuationBits && !letters.empty()) {
      letters.back() += c;
    } else {
      letters.emplace_back(1, c);
    }
  }
  return letters;
}

std::vector<std::string> lettersOfName(std::string_view name, std::size_t length) {
  std::vector<std::string> letters = lettersOf(name);
  if (letters.size() > length) {
    letters.resize(length);
  }
  return letters;
}

} // namespace

NamingBox::NamingBox(ui::Ui& ui, NamingRules rules) : ui_(&ui), rules_(std::move(rules)) {}

NamingBox::~NamingBox() {
  close();
}

void NamingBox::open(NameTarget target, std::string_view name) {
  target_ = target;
  letters_ = lettersOfName(name, rules_.length);
  row_ = 0;
  column_ = 0;
  open_ = true;
  core::logDebug("[port:story] setupNameSelectorBox (script_menu.c:2702): the {} name, {} letters",
                 target == NameTarget::Tamer ? "tamer's" : "partner's", letters_.size());
  publish();
  ui_->show(NamingBoxDocument);
}

void NamingBox::close() {
  if (open_) {
    open_ = false;
    ui_->hide(NamingBoxDocument);
  }
}

std::string NamingBox::name() const {
  std::string name;
  for (const std::string& letter : letters_) {
    name += letter;
  }
  // terminateNamingBuffer: the spaces at the end are not part of the name.
  while (!name.empty() && name.back() == ' ') {
    name.pop_back();
  }
  return name;
}

std::size_t NamingBox::gridRows() const {
  return rules_.rows.size() / 2;
}

std::size_t NamingBox::gridColumns() const {
  return rules_.columns * 2;
}

std::string NamingBox::letterAt(std::size_t row, std::size_t column) const {
  // tickNamingBox: the left side's rows, then the right side's (NAMING_CHAR_PAGES[page * 2 + 1]).
  const std::size_t side = column / rules_.columns;
  const std::vector<std::string> letters = lettersOf(rules_.rows.at(row + (side * gridRows())));
  const std::size_t at = column % rules_.columns;
  return at < letters.size() ? letters[at] : std::string{Blank};
}

bool NamingBox::press(const input::InputState& input) {
  if (!open_) {
    return false;
  }
  if (input.wasPressed("confirm")) {
    if (!onButtons()) {
      write();
    } else if (onOk()) {
      if (name().empty()) {
        core::logDebug("[port:story] tickNamingBox (script_menu.c:2850): OK with no name");
        return false;
      }
      core::logDebug("[port:story] tickNamingBox (script_menu.c:2855): the name is '{}'", name());
      return true;
    } else if (!letters_.empty()) {
      letters_.pop_back(); // namingDeleteLast
    }
  } else if (input.wasPressed("cancel")) {
    if (!letters_.empty()) {
      letters_.pop_back();
    }
  } else if (input.wasPressed("menu")) {
    goToOk();
  } else if (input.wasPressed("move_up")) {
    move(-1, 0);
  } else if (input.wasPressed("move_down")) {
    move(1, 0);
  } else if (input.wasPressed("move_left")) {
    move(0, -1);
  } else if (input.wasPressed("move_right")) {
    move(0, 1);
  } else {
    return false;
  }
  publish();
  return false;
}

void NamingBox::move(int rows, int columns) {
  // The cursor wraps round the grid and the row of the buttons (namingSelectionUp/Down/Left/Right);
  // on that row the left half is Back and the right half OK.
  const auto rowCount = static_cast<int>(gridRows() + 1);
  const auto columnCount = static_cast<int>(gridColumns());
  int row = static_cast<int>(row_) + rows;
  int column = static_cast<int>(column_);
  if (onButtons() && columns != 0) {
    column = std::cmp_less(column, rules_.columns) ? static_cast<int>(rules_.columns) : 0;
  } else {
    column += columns;
  }
  row_ = static_cast<std::size_t>(((row % rowCount) + rowCount) % rowCount);
  column_ = static_cast<std::size_t>(((column % columnCount) + columnCount) % columnCount);
}

void NamingBox::write() {
  const std::string letter = letterAt(row_, column_);
  if (letter == Blank && letters_.empty()) {
    core::logDebug("[port:story] tickNamingBox (script_menu.c:2812): a name starts with no space");
    return;
  }
  // The last letter is written again once the name is full; then the cursor goes to OK.
  if (letters_.size() == rules_.length) {
    letters_.back() = letter;
  } else {
    letters_.push_back(letter);
  }
  if (letters_.size() == rules_.length) {
    goToOk();
  }
}

void NamingBox::goToOk() {
  row_ = gridRows();
  column_ = rules_.columns;
}

void NamingBox::publish() {
  screens::ScreenSettings settings{
      {.name = std::format("{}.title", Prefix),
       .value = std::string{target_ == NameTarget::Tamer ? TamerTitle : PartnerTitle}},
      {.name = std::format("{}.length", Prefix), .value = static_cast<double>(rules_.length)},
      {.name = std::format("{}.rows", Prefix), .value = static_cast<double>(gridRows())},
      {.name = std::format("{}.columns", Prefix), .value = static_cast<double>(gridColumns())},
      {.name = std::format("{}.cursor_row", Prefix), .value = static_cast<double>(row_)},
      {.name = std::format("{}.cursor_column", Prefix), .value = static_cast<double>(column_)},
  };
  for (std::size_t letter = 0; letter < rules_.length; ++letter) {
    settings.push_back({.name = std::format("{}.name.{}", Prefix, letter),
                        .value = letter < letters_.size() ? letters_[letter] : std::string{}});
  }
  for (std::size_t row = 0; row < gridRows(); ++row) {
    for (std::size_t column = 0; column < gridColumns(); ++column) {
      settings.push_back({.name = std::format("{}.cell.{}.{}", Prefix, row, column),
                          .value = letterAt(row, column)});
    }
  }
  screens::publish(*ui_, settings, RevisionSetting);
}

} // namespace dwe::app
