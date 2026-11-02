// The textbox selection and the engine's pointer (the original has none): a click on an option
// picks it, and the pointer moving onto an option takes the cursor there without a sound
// (docs/modulos/game/story.md, "El puntero en los diálogos").

#include <dwe/core/error.hpp>
#include <dwe/game/story/dialogue.hpp>
#include <dwe/game/story/story_ports.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace {

using dwe::game::story::Dialogue;
using dwe::game::story::DialogueKeys;
using dwe::game::story::DialogueRules;
using dwe::game::story::DialogueScenePort;
using dwe::game::story::ResumeValue;
using dwe::game::story::SoundPort;
using dwe::game::story::StoryStatePort;

constexpr std::int64_t Triggers = 64;
constexpr std::int64_t NoCancel = 49;
constexpr std::int64_t CursorSound = 2;
constexpr std::int64_t AdvanceSound = 3;

class State final : public StoryStatePort {
public:
  [[nodiscard]] std::int64_t triggerCount() const override { return Triggers; }
  [[nodiscard]] std::int64_t variableCount() const override { return 0; }
  [[nodiscard]] dwe::core::Result<bool> trigger(std::int64_t /*trigger*/) const override {
    return false;
  }
  dwe::core::Result<void> setTrigger(std::int64_t /*trigger*/, bool /*set*/) override { return {}; }
  [[nodiscard]] dwe::core::Result<std::int64_t> variable(std::int64_t /*variable*/) const override {
    return 0;
  }
  dwe::core::Result<void> setVariable(std::int64_t /*variable*/, std::int64_t /*value*/) override {
    return {};
  }
};

class Scene final : public DialogueScenePort {
public:
  void freeze(std::int64_t /*speaker*/) override {}
  [[nodiscard]] bool onMap(std::int64_t /*speaker*/) const override { return false; }
};

class Sounds final : public SoundPort {
public:
  void playSound(std::int64_t /*slot*/, std::int64_t sound) override { played.push_back(sound); }
  std::vector<std::int64_t> played;
};

// A selection of three options on show, with its page turned and nothing pressed yet.
struct Selection {
  State state;
  Scene scene;
  Sounds sounds;
  Dialogue dialogue{state, scene,
                    DialogueRules{.openFrames = 0,
                                  .noCancelTrigger = NoCancel,
                                  .speechColumns = 21,
                                  .speechRows = 4,
                                  .openSound = {},
                                  .advanceSound = {.slot = 0, .sound = AdvanceSound},
                                  .cancelSound = {},
                                  .cursorSound = {.slot = 0, .sound = CursorSound},
                                  .refusedSound = {},
                                  .playerNameColor = 0,
                                  .partnerNameColor = 0,
                                  .characterNameColor = 0,
                                  .specialNameColor = 0}};

  Selection() {
    dialogue.bindSound(&sounds);
    REQUIRE(dialogue.showSelection("story.options", 3).has_value());
    frame({}); // the page turns; that frame reads nothing
    frame({}); // the fresh box's guard lets go
    sounds.played.clear();
  }

  std::optional<ResumeValue> frame(DialogueKeys keys) {
    dialogue.press(keys, false);
    dialogue.frameBegan();
    return dialogue.poll();
  }
};

} // namespace

TEST_CASE("a click on an option picks that option, whatever the cursor was on") {
  Selection selection;
  const auto picked = selection.frame({.clicked = 2});
  REQUIRE(picked.has_value());
  CHECK(*picked == ResumeValue{2});
  CHECK(selection.sounds.played == std::vector<std::int64_t>{AdvanceSound});
}

TEST_CASE(
    "the pointer moving onto an option takes the cursor there silently, and confirm picks it") {
  Selection selection;
  CHECK_FALSE(selection.frame({.pointed = 1}).has_value());
  REQUIRE(selection.dialogue.view().cursor.has_value());
  CHECK(*selection.dialogue.view().cursor == 1);
  CHECK(selection.sounds.played.empty());
  const auto picked = selection.frame({.confirm = true});
  REQUIRE(picked.has_value());
  CHECK(*picked == ResumeValue{1});
}

TEST_CASE(
    "the keys still move the cursor with its sound, and a pointer off the options is ignored") {
  Selection selection;
  CHECK_FALSE(selection.frame({.down = true}).has_value());
  CHECK(*selection.dialogue.view().cursor == 1);
  CHECK(selection.sounds.played == std::vector<std::int64_t>{CursorSound});
  // Rows that are not options (past the last one) neither move nor pick.
  CHECK_FALSE(selection.frame({.pointed = 3}).has_value());
  CHECK_FALSE(selection.frame({.clicked = 5}).has_value());
  CHECK(*selection.dialogue.view().cursor == 1);
}
