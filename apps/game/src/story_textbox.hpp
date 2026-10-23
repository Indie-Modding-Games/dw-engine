#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/game/story/dialogue.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/ui/ui.hpp>

#include <glm/vec2.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::app {

class FieldScene;

inline constexpr std::string_view StoryTextboxDocument = "story_textbox";

// The story's textboxes on screen: it publishes the dialogue's view (game/story: Dialogue) to the
// story_textbox document, its pages split in rows of coloured runs, and feeds the dialogue the
// player's keys.
class StoryTextbox {
public:
  StoryTextbox(ui::Ui& ui, session::StoryAdapter& story, const session::GameSession& session);
  ~StoryTextbox();
  StoryTextbox(const StoryTextbox&) = delete;
  StoryTextbox& operator=(const StoryTextbox&) = delete;
  StoryTextbox(StoryTextbox&&) = delete;
  StoryTextbox& operator=(StoryTextbox&&) = delete;

  // Once per engine tick while the field runs and no menu takes the keys.
  void press(const input::InputState& input);
  // Once per frame: publishes the view when it changed.
  void update(const FieldScene& scene, const assets::DigimonTable* species);

private:
  struct Run {
    std::string text;
    std::int64_t color = 0;
  };
  using Row = std::vector<Run>;

  [[nodiscard]] std::vector<Row> rowsOf(const game::story::DialogueView& view,
                                        const FieldScene& scene,
                                        const assets::DigimonTable* species) const;
  [[nodiscard]] std::string speakerName(const game::story::Speaker& speaker,
                                        const FieldScene& scene,
                                        const assets::DigimonTable* species) const;
  [[nodiscard]] std::int64_t nameColor(const game::story::Speaker& speaker) const;
  [[nodiscard]] std::string pageText(const std::string& key,
                                     const assets::DigimonTable* species) const;
  [[nodiscard]] std::optional<glm::vec2> originOf(const game::story::DialogueView& view,
                                                  const FieldScene& scene) const;

  ui::Ui* ui_;
  session::StoryAdapter* story_;
  const session::GameSession* session_;
  std::optional<std::uint32_t> published_;
  bool shown_ = false;
};

} // namespace dwe::app
