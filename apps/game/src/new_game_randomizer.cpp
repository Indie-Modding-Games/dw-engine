#include "new_game_randomizer.hpp"

#include <dwe/game/randomizer/run_settings.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dwe::app {

namespace {

using game::randomizer::LearnChance;
using game::randomizer::LearnChances;
using game::randomizer::RunSettings;

constexpr std::string_view Setting = "new_game_randomizer.";
constexpr std::string_view TogglePrefix = "toggle:";
constexpr std::size_t DraftLimit = 16;

// splitmix64's step: a new seed from the last one.
std::uint64_t nextSeed(std::uint64_t seed) {
  std::uint64_t value = seed + 0x9E3779B97F4A7C15U;
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9U;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBU;
  return (value ^ (value >> 31U)) & game::randomizer::SeedMask;
}

bool* optionNamed(RunSettings& run, std::string_view name) {
  if (name == "evolutions") {
    return &run.evolutions;
  }
  if (name == "starter") {
    return &run.starter;
  }
  if (name == "enemies") {
    return &run.enemies;
  }
  if (name == "items") {
    return &run.items;
  }
  if (name == "drops") {
    return &run.drops;
  }
  return nullptr;
}

std::string setting(std::string_view name) {
  return std::string{Setting} + std::string{name};
}

} // namespace

void NewGameRandomizer::open(std::uint64_t seed) {
  run_ = RunSettings{.seed = seed & game::randomizer::SeedMask};
  editing_ = false;
  invalid_ = false;
  draft_.clear();
}

NewGameRandomizer::Outcome NewGameRandomizer::act(std::string_view action) {
  if (action == "start") {
    if (editing_) {
      commit();
    }
    return Outcome::Start;
  }
  if (action == "back") {
    return Outcome::Back;
  }
  invalid_ = false;
  if (action.starts_with(TogglePrefix)) {
    if (bool* option = optionNamed(run_, action.substr(TogglePrefix.size()))) {
      *option = !*option;
    }
  } else if (action == "learn") {
    const auto at = std::ranges::find(LearnChances, run_.learnChance);
    const auto next = static_cast<std::size_t>(at - LearnChances.begin() + 1) % LearnChances.size();
    run_.learnChance = LearnChances.at(next);
  } else if (action == "preset:off") {
    run_ = RunSettings{.seed = run_.seed};
  } else if (action == "preset:chaos") {
    run_ = game::randomizer::chaos(run_.seed);
  } else if (action == "edit") {
    editing_ = true;
    draft_.clear();
  } else if (action == "erase") {
    if (draft_.empty()) {
      editing_ = false;
    } else {
      draft_.pop_back();
    }
  } else if (action == "commit") {
    commit();
  } else if (action == "paste") {
    editing_ = true;
    draft_.clear();
    type(clipboard_ ? clipboard_() : std::string{}, true);
  } else if (action == "reseed") {
    editing_ = false;
    run_.seed = nextSeed(run_.seed);
  }
  return Outcome::Changed;
}

void NewGameRandomizer::type(std::string_view text, bool enter) {
  if (!editing_) {
    return;
  }
  for (const char character : text) {
    if (game::randomizer::codeCharacter(character) && draft_.size() < DraftLimit) {
      draft_.push_back(character);
    }
  }
  if (enter) {
    commit();
  }
}

void NewGameRandomizer::commit() {
  editing_ = false;
  if (draft_.empty()) {
    return;
  }
  const auto parsed = game::randomizer::parseCode(draft_);
  draft_.clear();
  if (!parsed) {
    invalid_ = true;
    return;
  }
  if (parsed->options) {
    run_ = *parsed->options;
  }
  run_.seed = parsed->seed;
}

screens::ScreenSettings NewGameRandomizer::settings() const {
  std::string draft;
  for (const char character : draft_) {
    draft.push_back(character >= 'a' && character <= 'z' ? static_cast<char>(character - 'a' + 'A')
                                                         : character);
  }
  return {
      {.name = setting("evolutions"), .value = run_.evolutions},
      {.name = setting("learn"), .value = std::string{game::randomizer::nameOf(run_.learnChance)}},
      {.name = setting("starter"), .value = run_.starter},
      {.name = setting("enemies"), .value = run_.enemies},
      {.name = setting("items"), .value = run_.items},
      {.name = setting("drops"), .value = run_.drops},
      {.name = setting("seed"), .value = game::randomizer::seedText(run_.seed)},
      {.name = setting("code"), .value = game::randomizer::runCode(run_)},
      {.name = setting("active"), .value = run_.active()},
      {.name = setting("editing"), .value = editing_},
      {.name = setting("draft"), .value = draft},
      {.name = setting("invalid"), .value = invalid_}};
}

} // namespace dwe::app
