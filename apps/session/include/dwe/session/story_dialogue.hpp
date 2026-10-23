#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/story/dialogue.hpp>
#include <dwe/game/story/story_runner.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <cstdint>
#include <string_view>

namespace dwe::session {

// The ruleset section of the textboxes (rules/<id>/dialogue.yaml).
inline constexpr std::string_view DialogueSection = "dialogue";

core::Result<game::story::DialogueRules> readDialogueRules(const ruleset::Section& section);

// Spelled here because the runtime never depends on formats, as story_api is.
namespace story_dialogue_api {
inline constexpr std::string_view Player = "PLAYER";
inline constexpr std::string_view Partner = "PARTNER";
inline constexpr std::string_view ShowTextbox = "show_textbox";
inline constexpr std::string_view SetDialogOwner = "set_dialog_owner";
inline constexpr std::string_view SetSelection = "set_selection";
inline constexpr std::string_view SetTextboxMode = "set_textbox_mode";
inline constexpr std::string_view SetTextboxSize = "set_textbox_size";
inline constexpr std::string_view SetTextboxOrigin = "set_textbox_origin";
inline constexpr std::string_view FadeoutHud = "fadeout_hud";
} // namespace story_dialogue_api

// The story's textboxes in a game session: the dialogue model, held still with the runner while
// someone speaks. The app shows its view and feeds it the player's keys.
class StoryDialogue final : public game::story::DialogueScenePort {
public:
  StoryDialogue(game::story::StoryStatePort& state, game::story::DialogueRules rules);
  ~StoryDialogue() override;
  StoryDialogue(const StoryDialogue&) = delete;
  StoryDialogue& operator=(const StoryDialogue&) = delete;
  StoryDialogue(StoryDialogue&&) = delete;
  StoryDialogue& operator=(StoryDialogue&&) = delete;

  // Follows the runner's script and freezes the player through it; a new runner replaces the old.
  void attach(game::story::StoryRunner& runner);

  [[nodiscard]] game::story::Dialogue& dialogue() { return dialogue_; }
  [[nodiscard]] const game::story::Dialogue& dialogue() const { return dialogue_; }

  void freeze(std::int64_t speaker) override;
  [[nodiscard]] bool onMap(std::int64_t speaker) const override;

private:
  game::story::Dialogue dialogue_;
  game::story::StoryRunner* runner_ = nullptr;
};

} // namespace dwe::session
