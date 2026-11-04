#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/story/section_host.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/game/story/story_runner.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// The story's textboxes: the speech box with its speaker's name, the box of a set size, pages that
// wait for the player, and the choices of a selection (script_textbox.c, script_value.c:
// getSpeakerName, ui.c: the boxes' opening). The model keeps the original's flow and timing; how a
// box looks is the UI's.
namespace dwe::game::story {

// Who a speech box names, as the scripts number them (script_value.c: getSpeakerName): nobody, the
// player, the partner, a special speaker from FirstSpecialSpeaker on, else the character with that
// script number.
inline constexpr std::int64_t NoSpeaker = 0xFF;
inline constexpr std::int64_t PlayerSpeaker = 0xFD;
inline constexpr std::int64_t PartnerSpeaker = 0xFC;
inline constexpr std::int64_t FirstSpecialSpeaker = 0xC8;

enum class SpeakerKind : std::uint8_t { None, Player, Partner, Character, Special };

struct Speaker {
  SpeakerKind kind = SpeakerKind::None;
  // The character's script number, or the special speaker's number from 0.
  std::int64_t number = 0;

  friend bool operator==(const Speaker&, const Speaker&) = default;
};

[[nodiscard]] Speaker speakerOf(std::int64_t owner);

// A sound of the original's sound slots (SoundPort).
struct DialogueSound {
  std::int64_t slot = 0;
  std::int64_t sound = 0;
};

// The rules of the textboxes, from the ruleset's dialogue section.
struct DialogueRules {
  // Frames a box takes to grow from its speaker before it answers the player (ui.c:
  // renderUIBoxAnimated).
  std::int64_t openFrames = 0;
  // While this trigger is set a selection cannot be cancelled (script_textbox.c:
  // tickSelectionDialogue).
  std::int64_t noCancelTrigger = 0;
  // The size of the speech box in characters and rows, its speaker's name row included
  // (script_textbox.c: setupDialogueBox).
  std::int64_t speechColumns = 0;
  std::int64_t speechRows = 0;
  DialogueSound openSound;
  DialogueSound advanceSound;
  DialogueSound cancelSound;
  DialogueSound cursorSound;
  DialogueSound refusedSound;
  // The text colour of the speaker's name, by kind of speaker (script_textbox.c: showTextbox).
  std::int64_t playerNameColor = 0;
  std::int64_t partnerNameColor = 0;
  std::int64_t characterNameColor = 0;
  std::int64_t specialNameColor = 0;
};

// How a page of the speech box lets the script go on (script_textbox.c: tickConfirmDialogue).
enum class TextboxMode : std::uint8_t {
  // When the player confirms.
  Confirm = 0,
  // At once.
  Auto = 1,
  // Once the frames set_textbox_mode gave have passed, counted from that order.
  Timed = 2,
};

enum class BoxKind : std::uint8_t {
  // The speech box: the speaker's name on its first row.
  Speech,
  // The box of a set size (set_textbox_size): no name, and it closes after its last page.
  Sized,
};

// A point of the map, in the original's units.
struct WorldPoint {
  std::int64_t x = 0;
  std::int64_t y = 0;
  std::int64_t z = 0;

  friend bool operator==(const WorldPoint&, const WorldPoint&) = default;
};

// What the UI shows of the textboxes. `revision` counts the changes.
struct DialogueView {
  bool open = false;
  BoxKind kind = BoxKind::Speech;
  std::int64_t columns = 0;
  std::int64_t rows = 0;
  // Where the box grows from: a character's script number, or a point; neither for a box that
  // appears at once.
  std::optional<std::int64_t> originCharacter;
  std::optional<WorldPoint> originPoint;
  // From 0 to openFrames while the box grows; openFrames once it is open.
  std::int64_t openingFrame = 0;
  // The page on show: its text key (empty for none) and the speaker its first row names.
  std::string text;
  Speaker speaker;
  // The page waits for the player to confirm: the UI shows its prompt.
  bool waitingForConfirm = false;
  // The option the selection's cursor is on, from 0.
  std::optional<std::int64_t> cursor;
  std::uint32_t revision = 0;

  friend bool operator==(const DialogueView&, const DialogueView&) = default;
};

// The input the textboxes read: what was pressed since the last frame of the original.
struct DialogueKeys {
  bool confirm = false;
  bool cancel = false;
  bool up = false;
  bool down = false;
  // The pointer (the engine's, the original has none): the selection's option it moved onto,
  // from 0, which takes the cursor there without a sound, as the engine's menus follow the pointer;
  // and the option clicked, which takes the cursor there and picks it, as confirm would.
  std::optional<std::int64_t> pointed = std::nullopt;
  std::optional<std::int64_t> clicked = std::nullopt;
};

// What a textbox does outside the story: hold the player still while someone speaks.
class DialogueScenePort {
public:
  DialogueScenePort() = default;
  DialogueScenePort(const DialogueScenePort&) = delete;
  DialogueScenePort& operator=(const DialogueScenePort&) = delete;
  DialogueScenePort(DialogueScenePort&&) = delete;
  DialogueScenePort& operator=(DialogueScenePort&&) = delete;
  virtual ~DialogueScenePort() = default;

  // script_interp.c: scriptPauseGame with the speaker (NoSpeaker for a sized box).
  virtual void freeze(std::int64_t speaker) = 0;
  // Whether the speaker stands on the map, so its box grows from it (script_textbox.c:
  // setupBoxOrigin).
  [[nodiscard]] virtual bool onMap(std::int64_t speaker) const = 0;
};

class Dialogue final : public WaitSource, public ScriptFrameListener {
public:
  Dialogue(StoryStatePort& state, DialogueScenePort& scene, DialogueRules rules);

  void setRules(DialogueRules rules) { rules_ = rules; }
  [[nodiscard]] const DialogueRules& rules() const { return rules_; }
  // Where the sounds play; nullptr for silence.
  void bindSound(SoundPort* sound) { sound_ = sound; }

  // set_dialog_owner.
  void setOwner(std::int64_t owner);
  // show_textbox: shows the page of text key `text`; the section waits on the dialogue.
  Wait showText(std::string text);
  // set_selection: shows the options of text key `text`, one per row; the section gets back the
  // option picked from 0, or `count` when the player cancels.
  core::Result<Wait> showSelection(std::string text, std::int64_t count);
  // set_textbox_mode.
  core::Result<void> setMode(std::int64_t mode, std::int64_t frames);
  // set_textbox_size: opens the sized box, grown from `origin`'s character.
  core::Result<void> setSizedBox(std::int64_t origin, std::int64_t columns, std::int64_t rows);
  // set_textbox_origin: the next box with a speaker grows from this point.
  void setOrigin(WorldPoint point);
  // fadeout_hud: closes box `box` and waits a frame.
  FrameWait closeBox(std::int64_t box);

  // What was pressed in an engine tick, and whether confirm is held; kept until the next frame of
  // the original.
  void press(DialogueKeys pressed, bool confirmHeld);

  [[nodiscard]] const DialogueView& view() const { return view_; }

  std::optional<ResumeValue> poll() override;
  void scriptStarted() override;
  void frameBegan() override;
  void scriptStopped() override;

private:
  enum class Waiting : std::uint8_t { None, Page, Selection };

  struct Box {
    BoxKind kind = BoxKind::Speech;
    std::int64_t columns = 0;
    std::int64_t rows = 0;
    std::optional<std::int64_t> originCharacter;
    std::optional<WorldPoint> originPoint;
    std::int64_t openingFrame = 0;
    // A confirm held when the box opened counts only once it is let go (script_common.c:
    // setFreshDialogue as createTextbox opens it, isXPressedAfterDialogue).
    bool freshGuard = true;
  };

  void openBox(Box box);
  void openSpeechBox(std::int64_t owner);
  void closeAll();
  bool ensureBox(std::string_view text);
  void startPage(std::string text, std::int64_t speaker, Waiting waiting);
  [[nodiscard]] bool boxOpen() const;
  void tickBox(const DialogueKeys& keys);
  void tickPage(const DialogueKeys& keys);
  void tickSizedPage(const DialogueKeys& keys);
  void tickSelection(const DialogueKeys& keys);
  void finish(ResumeValue value);
  void play(const DialogueSound& sound);
  void publish();

  StoryStatePort* state_;
  DialogueScenePort* scene_;
  SoundPort* sound_ = nullptr;
  DialogueRules rules_;

  std::optional<Box> box_;
  std::int64_t owner_ = PlayerSpeaker;
  // After set_textbox_size the pages name nobody until set_dialog_owner (script_interp.c:
  // MAIN_D_80135000 == 2).
  bool namesHidden_ = false;
  TextboxMode mode_ = TextboxMode::Confirm;
  std::int64_t timedFrames_ = 0;
  std::optional<WorldPoint> nextOrigin_;

  std::string shownText_;
  Speaker shownSpeaker_;
  std::string pendingText_;
  Speaker pendingSpeaker_;
  bool pageOnShow_ = false;
  Waiting waiting_ = Waiting::None;
  std::int64_t optionCount_ = 0;
  std::int64_t cursor_ = 0;
  std::optional<ResumeValue> result_;
  // The sized box closes after a confirmed page unless another page follows in the same frame.
  bool closeAfterPage_ = false;
  // The player skipped the sized box's pages: the pages that follow it show nothing.
  bool skipPages_ = false;

  DialogueKeys latched_;
  bool confirmHeld_ = false;
  DialogueView view_;
};

} // namespace dwe::game::story
