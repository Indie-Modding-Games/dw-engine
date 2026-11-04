// A new script section starts with boxes that close on confirm (script_interp.c: callScriptSection
// sets TEXTBOX_CLOSE_MODE 0; its initializeTextbox, script_textbox.c:412, clears AUTOCLOSE_FRAMES),
// whatever mode the last section left (docs/modulos/game/story.md).

#include <dwe/core/error.hpp>
#include <dwe/game/story/dialogue.hpp>
#include <dwe/game/story/story_ports.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>

namespace {

using dwe::game::story::Dialogue;
using dwe::game::story::DialogueKeys;
using dwe::game::story::DialogueRules;
using dwe::game::story::DialogueScenePort;
using dwe::game::story::ResumeValue;
using dwe::game::story::StoryStatePort;

constexpr std::int64_t AutoMode = 1;
constexpr int FramesWatched = 12;

class State final : public StoryStatePort {
public:
  [[nodiscard]] std::int64_t triggerCount() const override { return 64; }
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

struct Talk {
  State state;
  Scene scene;
  Dialogue dialogue{state, scene,
                    DialogueRules{.openFrames = 0,
                                  .noCancelTrigger = 49,
                                  .speechColumns = 21,
                                  .speechRows = 4,
                                  .openSound = {},
                                  .advanceSound = {},
                                  .cancelSound = {},
                                  .cursorSound = {},
                                  .refusedSound = {},
                                  .playerNameColor = 0,
                                  .partnerNameColor = 0,
                                  .characterNameColor = 0,
                                  .specialNameColor = 0}};

  std::optional<ResumeValue> frame(DialogueKeys keys) {
    dialogue.press(keys, false);
    dialogue.frameBegan();
    return dialogue.poll();
  }

  // Frames with nothing pressed until the page is done; false when it is still on show.
  bool doneUnpressed() {
    for (int i = 0; i < FramesWatched; ++i) {
      if (frame({}).has_value()) {
        return true;
      }
    }
    return false;
  }
};

} // namespace

TEST_CASE("a section that leaves the boxes closing on their own does not pass it to the next") {
  Talk talk;
  talk.dialogue.scriptStarted();
  REQUIRE(talk.dialogue.setMode(AutoMode, 0).has_value());
  static_cast<void>(talk.dialogue.showText("story.first"));
  CHECK(talk.doneUnpressed()); // the auto mode closes the page by itself
  talk.dialogue.scriptStopped();

  // The next section: its page waits for confirm.
  talk.dialogue.scriptStarted();
  static_cast<void>(talk.dialogue.showText("story.second"));
  CHECK_FALSE(talk.doneUnpressed());
  bool done = false;
  for (int i = 0; i < FramesWatched && !done; ++i) {
    done = talk.frame({.confirm = true}).has_value();
  }
  CHECK(done);
}
