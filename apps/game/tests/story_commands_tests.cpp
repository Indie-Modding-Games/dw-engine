// The UI API's story, naming and dialog commands (story_commands.hpp, docs/modding/ui-api.md):
// each gives the story's boxes the press the player's button would, only while the box waits for
// it, so a page still gets the confirm the story waits for and a selection the option it returns;
// and the game takes them from a UI addon only in their window.

#include <dwe/core/error.hpp>
#include <dwe/game/story/dialogue.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/ui/ui.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "story_commands.hpp"
#include "ui_addon_setting.hpp"

namespace dwe::app {
namespace {

using game::story::Dialogue;
using game::story::DialogueInput;
using game::story::DialogueKeys;
using game::story::DialogueRules;
using game::story::ResumeValue;

constexpr std::int64_t AdvanceSound = 3;

class State final : public game::story::StoryStatePort {
public:
  [[nodiscard]] std::int64_t triggerCount() const override { return 64; }
  [[nodiscard]] std::int64_t variableCount() const override { return 0; }
  [[nodiscard]] core::Result<bool> trigger(std::int64_t /*trigger*/) const override {
    return false;
  }
  core::Result<void> setTrigger(std::int64_t /*trigger*/, bool /*set*/) override { return {}; }
  [[nodiscard]] core::Result<std::int64_t> variable(std::int64_t /*variable*/) const override {
    return 0;
  }
  core::Result<void> setVariable(std::int64_t /*variable*/, std::int64_t /*value*/) override {
    return {};
  }
};

class Scene final : public game::story::DialogueScenePort {
public:
  void freeze(std::int64_t /*speaker*/) override {}
  [[nodiscard]] bool onMap(std::int64_t /*speaker*/) const override { return false; }
};

class Sounds final : public game::story::SoundPort {
public:
  void playSound(std::int64_t /*slot*/, std::int64_t sound) override { played.push_back(sound); }
  std::vector<std::int64_t> played;
};

// The story's boxes, and the story waiting on them.
struct Talk {
  State state;
  Scene scene;
  Sounds sounds;
  Dialogue dialogue{state, scene,
                    DialogueRules{.openFrames = 0,
                                  .noCancelTrigger = 49,
                                  .speechColumns = 21,
                                  .speechRows = 4,
                                  .openSound = {},
                                  .advanceSound = {.slot = 0, .sound = AdvanceSound},
                                  .cancelSound = {},
                                  .cursorSound = {},
                                  .refusedSound = {},
                                  .playerNameColor = 0,
                                  .partnerNameColor = 0,
                                  .characterNameColor = 0,
                                  .specialNameColor = 0}};

  Talk() { dialogue.bindSound(&sounds); }

  // One frame of the original with what a command pressed, as StoryTextbox::handle gives it.
  std::optional<ResumeValue> frame(const ui::UiCommand& command) {
    const auto keys = storyKeysOf(command, storyInputOf(dialogue.input()), dialogue.optionCount());
    dialogue.press(keys.value_or(DialogueKeys{}), false);
    dialogue.frameBegan();
    return dialogue.poll();
  }
};

ui::UiCommand command(std::string_view name, ui::UiValue value = {}) {
  return {.name = std::string{name}, .value = std::move(value)};
}

} // namespace

TEST_CASE("story.advance gives a waiting page its confirm, and nothing before it waits") {
  Talk talk;
  static_cast<void>(talk.dialogue.showText("story.first"));
  // The frame the page turns reads nothing: the box does not wait yet.
  CHECK(talk.dialogue.input() == DialogueInput::None);
  CHECK_FALSE(talk.frame(command(StoryAdvanceCommand)).has_value());
  CHECK(talk.dialogue.input() == DialogueInput::Page);
  CHECK(talk.dialogue.pagesShown() == 1);
  // A choice is no press for a page.
  CHECK_FALSE(talk.frame(command(StoryChooseCommand, 0.0)).has_value());
  const auto done = talk.frame(command(StoryAdvanceCommand));
  REQUIRE(done.has_value());
  CHECK(*done == ResumeValue{});
  CHECK(talk.sounds.played == std::vector<std::int64_t>{AdvanceSound});
  CHECK(talk.dialogue.input() == DialogueInput::None);
}

TEST_CASE("story.choose picks the option the story gets back, and only an option it has") {
  Talk talk;
  REQUIRE(talk.dialogue.showSelection("story.options", 3).has_value());
  CHECK_FALSE(talk.frame(command(StoryChooseCommand, 1.0)).has_value());
  REQUIRE(talk.dialogue.input() == DialogueInput::Selection);
  CHECK(talk.dialogue.optionCount() == 3);
  CHECK_FALSE(talk.frame(command(StoryChooseCommand, 3.0)).has_value());
  CHECK_FALSE(talk.frame(command(StoryChooseCommand, 1.5)).has_value());
  CHECK_FALSE(talk.frame(command(StoryChooseCommand, std::string{"1"})).has_value());
  const auto picked = talk.frame(command(StoryChooseCommand, 2.0));
  REQUIRE(picked.has_value());
  CHECK(*picked == ResumeValue{2});
}

TEST_CASE("story.advance in a selection takes the option under the cursor") {
  Talk talk;
  REQUIRE(talk.dialogue.showSelection("story.options", 2).has_value());
  static_cast<void>(talk.frame(command("other")));
  const auto picked = talk.frame(command(StoryAdvanceCommand));
  REQUIRE(picked.has_value());
  CHECK(*picked == ResumeValue{0});
}

TEST_CASE("the story's commands give no press while the boxes wait for nothing") {
  CHECK_FALSE(storyKeysOf(command(StoryAdvanceCommand), StoryInput::None, 0).has_value());
  CHECK_FALSE(storyKeysOf(command(StoryCancelCommand), StoryInput::None, 0).has_value());
  CHECK_FALSE(storyKeysOf(command(StoryChooseCommand, 0.0), StoryInput::Page, 2).has_value());
  CHECK_FALSE(storyKeysOf(command("dialog_answer", true), StoryInput::Page, 0).has_value());
  const auto cancel = storyKeysOf(command(StoryCancelCommand), StoryInput::Choice, 2);
  REQUIRE(cancel.has_value());
  CHECK(cancel->cancel);
  CHECK_FALSE(cancel->confirm);
}

TEST_CASE("the naming commands ask for letters, an erase or OK") {
  const auto write = namingAskOf(command(NamingWriteCommand, std::string{"AB"}));
  REQUIRE(write.has_value());
  CHECK(write->text == "AB");
  CHECK_FALSE(namingAskOf(command(NamingWriteCommand, 1.0)).has_value());
  CHECK(namingAskOf(command(NamingEraseCommand)).value_or(NamingAsk{}).erase);
  CHECK(namingAskOf(command(NamingAcceptCommand)).value_or(NamingAsk{}).accept);
  CHECK_FALSE(namingAskOf(command(StoryAdvanceCommand)).has_value());
}

TEST_CASE("an addon's story, naming and dialog commands go only in their window") {
  const StoryWindows closed{};
  const StoryWindows page{.input = StoryInput::Page};
  const StoryWindows choice{.input = StoryInput::Choice};
  const StoryWindows naming{.naming = true};
  const StoryWindows dialog{.dialog = true};
  for (const std::string_view story : {StoryAdvanceCommand, StoryCancelCommand}) {
    CHECK(addonMaySend(story));
    CHECK_FALSE(storyCommandInWindow(story, closed));
    CHECK(storyCommandInWindow(story, page));
    CHECK(storyCommandInWindow(story, choice));
  }
  CHECK(addonMaySend(StoryChooseCommand));
  CHECK_FALSE(storyCommandInWindow(StoryChooseCommand, page));
  CHECK(storyCommandInWindow(StoryChooseCommand, choice));
  for (const std::string_view name :
       {NamingWriteCommand, NamingEraseCommand, NamingAcceptCommand}) {
    CHECK(addonMaySend(name));
    CHECK_FALSE(storyCommandInWindow(name, page));
    CHECK(storyCommandInWindow(name, naming));
  }
  CHECK(addonMaySend(DialogAnswerCommand));
  CHECK_FALSE(storyCommandInWindow(DialogAnswerCommand, naming));
  CHECK(storyCommandInWindow(DialogAnswerCommand, dialog));
  // Any other command is not theirs to refuse.
  CHECK(storyCommandInWindow("bag_use", closed));
}

} // namespace dwe::app
