#include "naming_box.hpp"

#include <dwe/core/log.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <array>
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
constexpr std::array<std::string_view, 4> MoveActions{"move_up", "move_down", "move_left",
                                                      "move_right"};
// From a button the cursor goes into the grid on this row: right to the left side's first column,
// left to the right side's last (NAMING_ROLLOVER: 0x19, 0x19 + 0x31).
constexpr std::size_t ButtonEntryRow = 5;
constexpr double NoCell = -1.0;

// The letters of a text: one UTF-8 character each.
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

NamingBox::NamingBox(ui::Ui& ui, NamingRules rules, SceneSounds sounds)
    : ui_(&ui), rules_(std::move(rules)), sounds_(sounds) {}

NamingBox::~NamingBox() {
  close();
}

void NamingBox::open(NameTarget target, std::string_view name) {
  target_ = target;
  letters_ = lettersOfName(name, rules_.length);
  side_ = 0;
  row_ = 0;
  column_ = 0;
  button_.reset();
  held_.clear();
  typed_.clear();
  enter_ = false;
  typing_ = false;
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

void NamingBox::type(std::string_view text, bool enter) {
  if (!open_) {
    return;
  }
  typed_ += text;
  enter_ = enter_ || enter;
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

std::string NamingBox::letterAt(std::size_t side, std::size_t row, std::size_t column) const {
  // tickNamingBox: the left side's rows, then the right side's (NAMING_CHAR_PAGES[page * 2 + 1]).
  const std::vector<std::string> letters = lettersOf(rules_.rows.at(row + (side * sideRows())));
  return column < letters.size() ? letters[column] : std::string{Blank};
}

bool NamingBox::knows(std::string_view letter) const {
  if (letter == Blank) {
    return true;
  }
  for (const std::string& row : rules_.rows) {
    for (const std::string& known : lettersOf(row)) {
      if (known == letter) {
        return true;
      }
    }
  }
  return false;
}

bool NamingBox::press(const input::InputState& input) {
  if (!open_) {
    return false;
  }
  // The keyboard's text comes first: its keys are also the movement and confirm actions.
  if (!typed_.empty() || (enter_ && typing_)) {
    const bool taken = takeTyping();
    held_.clear();
    publish();
    return taken;
  }
  enter_ = false;
  const bool taken = pressButtons(input);
  publish();
  return taken;
}

bool NamingBox::takeTyping() {
  const std::string typed = std::exchange(typed_, {});
  const bool enter = std::exchange(enter_, false);
  for (const std::string& letter : lettersOf(typed)) {
    if (!knows(letter)) {
      play(sounds_.refused);
      continue;
    }
    typing_ = true;
    write(letter);
  }
  return enter && typing_ && accept();
}

bool NamingBox::pressButtons(const input::InputState& input) {
  // tickNamingBox: the square (`extra`) clears the whole name before anything else.
  if (input.wasPressed("extra")) {
    letters_.clear();
    core::logDebug("[port:story] tickNamingBox (script_menu.c:2794): the name is cleared");
    play(sounds_.confirm);
    return false;
  }
  if (input.wasPressed("confirm")) {
    if (!button_) {
      write(letterAt(side_, row_, column_));
    } else if (*button_ == Button::Ok) {
      return accept();
    } else {
      deleteLast();
    }
  } else if (input.wasPressed("alt")) {
    // tickNamingBox: the circle (ALT_BUTTON) puts the cursor on Back.
    goTo(Button::Back);
    play(sounds_.cursor);
  } else if (input.wasPressed("cancel")) {
    deleteLast();
  } else if (input.wasPressed("menu")) {
    // tickNamingBox: Start puts the cursor on OK.
    goTo(Button::Ok);
    play(sounds_.cursor);
  } else {
    repeatMoves(input);
  }
  return false;
}

void NamingBox::repeatMoves(const input::InputState& input) {
  for (const std::string_view action : MoveActions) {
    if (input.wasPressed(action)) {
      held_ = action;
      heldTicks_ = 0;
      move(action);
      return;
    }
  }
  if (held_.empty() || !input.isDown(held_)) {
    held_.clear();
    return;
  }
  // script_common.c: a held key moves again after the delay, then every `repeatFrames` frames.
  ++heldTicks_;
  const std::uint64_t delay =
      static_cast<std::uint64_t>(rules_.repeatDelayFrames) * rules_.frameTicks;
  const std::uint64_t period = static_cast<std::uint64_t>(rules_.repeatFrames) * rules_.frameTicks;
  if (heldTicks_ >= delay && (heldTicks_ - delay) % period == 0) {
    move(held_);
  }
}

void NamingBox::move(std::string_view action) {
  // namingSelectionUp/Down/Left/Right: up and down wrap round each side's rows (and swap the
  // buttons); right from a side's last column goes to the other side, or from the right side to
  // Back; left from the left side's first column goes to Back, from the right side's to the left
  // side; from a button right enters the left side and left the right side.
  typing_ = false;
  play(sounds_.cursor);
  const std::size_t last = rules_.columns - 1;
  if (button_) {
    if (action == "move_up" || action == "move_down") {
      button_ = *button_ == Button::Back ? Button::Ok : Button::Back;
    } else {
      side_ = action == "move_right" ? 0 : 1;
      column_ = action == "move_right" ? 0 : last;
      row_ = ButtonEntryRow;
      button_.reset();
    }
    return;
  }
  if (action == "move_up") {
    row_ = row_ == 0 ? sideRows() - 1 : row_ - 1;
  } else if (action == "move_down") {
    row_ = row_ + 1 == sideRows() ? 0 : row_ + 1;
  } else if (action == "move_right") {
    if (column_ < last) {
      ++column_;
    } else if (side_ == 0) {
      side_ = 1;
      column_ = 0;
    } else {
      button_ = Button::Back;
    }
  } else if (column_ > 0) {
    --column_;
  } else if (side_ == 1) {
    side_ = 0;
    column_ = last;
  } else {
    button_ = Button::Back;
  }
}

void NamingBox::write(const std::string& letter) {
  if (letter == Blank && letters_.empty()) {
    core::logDebug("[port:story] tickNamingBox (script_menu.c:2813): a name starts with no space");
    play(sounds_.refused);
    return;
  }
  // The last letter is written again once the name is full; then the cursor goes to OK.
  if (letters_.size() == rules_.length) {
    letters_.back() = letter;
  } else {
    letters_.push_back(letter);
  }
  if (letters_.size() == rules_.length) {
    goTo(Button::Ok);
  }
  play(sounds_.confirm);
}

void NamingBox::deleteLast() {
  if (letters_.empty()) {
    return;
  }
  // namingDeleteLast: the American version steps back before it clears, so a full name loses two.
  if (rules_.fullDeleteTakesTwo && letters_.size() == rules_.length && letters_.size() > 1) {
    letters_.resize(letters_.size() - 2);
  } else {
    letters_.pop_back();
  }
  play(sounds_.confirm);
}

bool NamingBox::accept() {
  if (name().empty()) {
    core::logDebug("[port:story] tickNamingBox (script_menu.c:2850): OK with no name");
    play(sounds_.refused);
    return false;
  }
  core::logDebug("[port:story] tickNamingBox (script_menu.c:2855): the name is '{}'", name());
  play(sounds_.confirm);
  return true;
}

void NamingBox::goTo(Button button) {
  button_ = button;
}

void NamingBox::play(const game::story::DialogueSound& sound) {
  if (sound_ != nullptr) {
    sound_->playSound(sound.slot, sound.sound);
  }
}

void NamingBox::publish() {
  const std::size_t columns = rules_.columns * 2;
  screens::ScreenSettings settings{
      {.name = std::format("{}.title", Prefix),
       .value = std::string{target_ == NameTarget::Tamer ? TamerTitle : PartnerTitle}},
      {.name = std::format("{}.length", Prefix), .value = static_cast<double>(rules_.length)},
      {.name = std::format("{}.rows", Prefix), .value = static_cast<double>(sideRows())},
      {.name = std::format("{}.columns", Prefix), .value = static_cast<double>(columns)},
      {.name = std::format("{}.cursor_row", Prefix),
       .value = button_ ? NoCell : static_cast<double>(row_)},
      {.name = std::format("{}.cursor_column", Prefix),
       .value = button_ ? NoCell : static_cast<double>((side_ * rules_.columns) + column_)},
      {.name = std::format("{}.cursor_button", Prefix),
       .value = button_ ? static_cast<double>(std::to_underlying(*button_)) : NoCell},
  };
  for (std::size_t letter = 0; letter < rules_.length; ++letter) {
    settings.push_back({.name = std::format("{}.name.{}", Prefix, letter),
                        .value = letter < letters_.size() ? letters_[letter] : std::string{}});
  }
  for (std::size_t row = 0; row < sideRows(); ++row) {
    for (std::size_t column = 0; column < columns; ++column) {
      settings.push_back(
          {.name = std::format("{}.cell.{}.{}", Prefix, row, column),
           .value = letterAt(column / rules_.columns, row, column % rules_.columns)});
    }
  }
  screens::publish(*ui_, settings, RevisionSetting);
}

} // namespace dwe::app
