#pragma once

#include <dwe/assets/digimon_table.hpp>
#include <dwe/game/story/dialogue.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/screens/name_keys.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_adapter.hpp>
#include <dwe/ui/ui.hpp>

#include <glm/vec2.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "story_text_rows.hpp"

namespace dwe::app {

class FieldScene;

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
  // A screen that says the owner's lines in its own box (the jukebox, whose showMapHeadTextbox
  // writes over the story's box) covers the story's box while it is up, and after it until the
  // story changes the box (the original's box closed with the screen's last line).
  void cover(bool covered);

private:
  [[nodiscard]] std::vector<TextRow> rowsOf(const game::story::DialogueView& view,
                                            const FieldScene& scene,
                                            const assets::DigimonTable* species) const;
  [[nodiscard]] std::string speakerName(const game::story::Speaker& speaker,
                                        const FieldScene& scene,
                                        const assets::DigimonTable* species) const;
  [[nodiscard]] std::int64_t nameColor(const game::story::Speaker& speaker) const;
  [[nodiscard]] std::string tamerName() const;
  [[nodiscard]] std::string partnerName(const screens::NameKeys& names) const;
  [[nodiscard]] std::string pageText(const std::string& key,
                                     const assets::DigimonTable* species) const;
  [[nodiscard]] static std::optional<glm::vec2> originOf(const game::story::DialogueView& view,
                                                         const FieldScene& scene);

  ui::Ui* ui_;
  session::StoryAdapter* story_;
  const session::GameSession* session_;
  std::optional<std::uint32_t> published_;
  bool shown_ = false;
  bool covered_ = false;
  // The view's revision when the cover went: the box stays hidden while the view keeps it.
  std::optional<std::uint32_t> staleRevision_;
};

} // namespace dwe::app
