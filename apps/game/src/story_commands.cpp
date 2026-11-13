#include "story_commands.hpp"

#include <cmath>
#include <variant>

namespace dwe::app {

namespace {

constexpr std::string_view NamingBoxScreen = "naming_box";
constexpr std::string_view DialogScreen = "dialog";

} // namespace

std::string_view storyInputName(StoryInput input) {
  switch (input) {
  case StoryInput::Page:
    return "page";
  case StoryInput::Choice:
    return "choice";
  case StoryInput::None:
    break;
  }
  return "";
}

StoryInput storyInputOf(game::story::DialogueInput input) {
  switch (input) {
  case game::story::DialogueInput::Page:
    return StoryInput::Page;
  case game::story::DialogueInput::Selection:
    return StoryInput::Choice;
  case game::story::DialogueInput::None:
    break;
  }
  return StoryInput::None;
}

std::optional<game::story::DialogueKeys> storyKeysOf(const ui::UiCommand& command, StoryInput input,
                                                     std::int64_t options) {
  if (!isStoryCommand(command.name) || input == StoryInput::None) {
    return std::nullopt;
  }
  if (command.name == StoryAdvanceCommand) {
    return game::story::DialogueKeys{.confirm = true};
  }
  if (command.name == StoryCancelCommand) {
    return game::story::DialogueKeys{.cancel = true};
  }
  // story_choose: a whole option of the selection on show.
  const double* option = std::get_if<double>(&command.value);
  if (input != StoryInput::Choice || option == nullptr || *option != std::floor(*option) ||
      *option < 0.0 || *option >= static_cast<double>(options)) {
    return std::nullopt;
  }
  return game::story::DialogueKeys{.clicked = static_cast<std::int64_t>(*option)};
}

bool isStoryCommand(std::string_view name) {
  return name == StoryAdvanceCommand || name == StoryChooseCommand || name == StoryCancelCommand;
}

std::optional<NamingAsk> namingAskOf(const ui::UiCommand& command) {
  if (command.name == NamingWriteCommand) {
    const std::string* text = std::get_if<std::string>(&command.value);
    if (text == nullptr) {
      return std::nullopt;
    }
    return NamingAsk{.text = *text, .erase = false, .accept = false};
  }
  if (command.name == NamingEraseCommand) {
    return NamingAsk{.text = {}, .erase = true, .accept = false};
  }
  if (command.name == NamingAcceptCommand) {
    return NamingAsk{.text = {}, .erase = false, .accept = true};
  }
  return std::nullopt;
}

StoryWindows storyWindowsOf(const ui::Ui& ui) {
  const ui::UiValue input = ui.setting(StoryInputSetting);
  const std::string* name = std::get_if<std::string>(&input);
  StoryWindows windows{.naming = ui.isScreenOpen(NamingBoxScreen),
                       .dialog = ui.isScreenOpen(DialogScreen)};
  if (name != nullptr && *name == storyInputName(StoryInput::Page)) {
    windows.input = StoryInput::Page;
  } else if (name != nullptr && *name == storyInputName(StoryInput::Choice)) {
    windows.input = StoryInput::Choice;
  }
  return windows;
}

bool storyCommandInWindow(std::string_view command, StoryWindows windows) {
  if (command == StoryChooseCommand) {
    return windows.input == StoryInput::Choice;
  }
  if (isStoryCommand(command)) {
    return windows.input != StoryInput::None;
  }
  if (command == NamingWriteCommand || command == NamingEraseCommand ||
      command == NamingAcceptCommand) {
    return windows.naming;
  }
  if (command == DialogAnswerCommand) {
    return windows.dialog;
  }
  return true;
}

} // namespace dwe::app
