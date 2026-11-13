#pragma once

#include <dwe/game/story/dialogue.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::app {

// The UI API's story, naming and dialog commands (docs/modding/ui-api.md, namespaces story, naming
// and dialog): what a UI may ask of the story's boxes while they wait for the player, the same
// presses the player's buttons give. The game takes the player's buttons itself; these let a UI
// that draws the boxes its own way (an addon, the pointer) answer them too.

// story.advance(): confirm, as the confirm button (a page goes on, a selection takes its cursor).
inline constexpr std::string_view StoryAdvanceCommand = "story_advance";
// story.choose(option): picks an option of a selection, from 0, as a click on it.
inline constexpr std::string_view StoryChooseCommand = "story_choose";
// story.cancel(): cancel, as the cancel button.
inline constexpr std::string_view StoryCancelCommand = "story_cancel";

// naming.write(text): writes the letters of the grid in text, as the keyboard's typing.
inline constexpr std::string_view NamingWriteCommand = "naming_write";
// naming.erase(): takes the last letter back, as cancel.
inline constexpr std::string_view NamingEraseCommand = "naming_erase";
// naming.accept(): OK, which takes a name that is not empty.
inline constexpr std::string_view NamingAcceptCommand = "naming_accept";

// dialog.answer(option): the save dialog's answer, true only for yes.
inline constexpr std::string_view DialogAnswerCommand = "dialog_answer";

// What the story's textboxes wait for from the player, as the UI reads it
// (story_textbox.input): "page", "choice" or "" for nothing.
inline constexpr std::string_view StoryInputSetting = "story_textbox.input";
// How many pages the textboxes have shown (story_textbox.page): one more with each page.
inline constexpr std::string_view StoryPageSetting = "story_textbox.page";

enum class StoryInput : std::uint8_t { None, Page, Choice };

[[nodiscard]] std::string_view storyInputName(StoryInput input);
[[nodiscard]] StoryInput storyInputOf(game::story::DialogueInput input);

// The presses a story command gives the textboxes that wait for `input` with `options` options;
// nullopt when it is no story command or the boxes take no such press now (a choice outside a
// selection, an option it lacks, anything while they wait for nothing).
[[nodiscard]] std::optional<game::story::DialogueKeys>
storyKeysOf(const ui::UiCommand& command, StoryInput input, std::int64_t options);

// Whether a command is one of the story's (story_advance, story_choose, story_cancel).
[[nodiscard]] bool isStoryCommand(std::string_view name);

// What a naming command asks of the naming box.
struct NamingAsk {
  // The letters to write (naming_write), as typed.
  std::string text;
  bool erase = false;
  bool accept = false;
};

// The naming box's request in a naming command; nullopt for another command or a bad value.
[[nodiscard]] std::optional<NamingAsk> namingAskOf(const ui::UiCommand& command);

// The windows in which the game takes the story's, the naming box's and the dialog's commands from
// a UI addon, as the game's own boxes take the player's buttons: the textboxes waiting for a page
// or a choice, the naming box open, the dialog open.
struct StoryWindows {
  StoryInput input = StoryInput::None;
  bool naming = false;
  bool dialog = false;
};

[[nodiscard]] StoryWindows storyWindowsOf(const ui::Ui& ui);

// Whether a command of the story, the naming box or the dialog comes in its window; any other
// command is not theirs to refuse.
[[nodiscard]] bool storyCommandInWindow(std::string_view command, StoryWindows windows);

} // namespace dwe::app
