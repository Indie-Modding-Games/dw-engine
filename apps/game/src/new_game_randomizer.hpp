#pragma once

#include <dwe/game/randomizer/run_settings.hpp>
#include <dwe/screens/screen_settings.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace dwe::app {

inline constexpr std::string_view NewGameRandomizerDocument = "new_game_randomizer";
inline constexpr std::string_view NewGameRandomizerCommand = "new_game_randomizer";

// The randomizer's setup screen (ui/new_game_randomizer, docs/modules/game/randomizer.md): after a
// new game's mode and rules, for a mode that offers it. Each option toggles, the learning chance
// steps through its values and the seed is a new random one, typed or pasted; the run code shows
// what a friend types to play the same run.
class NewGameRandomizer {
public:
  // The system clipboard's text, for the paste button; without it the button pastes nothing.
  void setClipboard(std::function<std::string()> clipboard) { clipboard_ = std::move(clipboard); }

  // Starts over with every option off and this seed.
  void open(std::uint64_t seed);

  enum class Outcome : std::uint8_t { Changed, Start, Back };
  // One of the document's actions (the command's value): toggle:<option>, learn, preset:off,
  // preset:chaos, edit, erase, commit, paste, reseed, start or back.
  [[nodiscard]] Outcome act(std::string_view action);

  // Whether the seed field takes the keyboard's letters.
  [[nodiscard]] bool takesText() const { return editing_; }
  // Letters typed into the seed field; enter keeps them when they make a seed or a run code.
  void type(std::string_view text, bool enter);

  [[nodiscard]] const game::randomizer::RunSettings& run() const { return run_; }
  [[nodiscard]] screens::ScreenSettings settings() const;

private:
  void commit();

  std::function<std::string()> clipboard_;
  game::randomizer::RunSettings run_;
  bool editing_ = false;
  bool invalid_ = false;
  std::string draft_;
};

} // namespace dwe::app
