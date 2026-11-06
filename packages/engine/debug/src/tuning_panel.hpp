#pragma once

#include <dwe/debug/tuning.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "tuning_model.hpp"

namespace dwe::debug {

// The overlay's tuning window (R3.4): the rulesets and their values, edited live; the game clock;
// and what the systems see, with the tabs they add.
class TuningPanel {
public:
  explicit TuningPanel(const TuningTarget& target) : model_(target) {}

  // Adds a system's tab after the built-in ones.
  void addTab(TuningTab tab) { tabs_.push_back(std::move(tab)); }

  // Builds the window; open turns false when it is closed.
  void draw(bool& open);

private:
  // A text field's contents while it is edited.
  using EditBuffer = std::array<char, 256>;

  void drawRulesetTab();
  void drawRulesetChooser();
  void drawOutcome();
  void drawReload();
  void drawSection(const TuningSection& section);
  void drawNode(const TuningSection& section, const TuningNode& node);
  void drawValue(const TuningSection& section, const TuningNode& node);
  void drawClockTab();
  static void drawClockSpeed(ruleset::GameClock& clock);
  void drawClockJump(ruleset::GameClock& clock);
  void drawSystemsTab();
  static void drawCustomTab(const TuningTab& tab);

  // The buffer of a value's text field, filled with its value when the field is new or the ruleset
  // changed.
  EditBuffer& bufferOf(const TuningSection& section, const TuningNode& node);

  TuningModel model_;
  std::vector<TuningTab> tabs_;
  std::map<std::string, EditBuffer, std::less<>> buffers_;
  std::uint64_t buffersRevision_ = 0;
  std::int32_t jumpHour_ = 0;
  std::int32_t jumpMinute_ = 0;
};

} // namespace dwe::debug
