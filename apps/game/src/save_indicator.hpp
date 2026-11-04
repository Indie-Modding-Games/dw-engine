#pragma once

#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

namespace dwe::app {

// The save indicator (an engine improvement; the original has no autosave and says nothing when
// its save machine writes): a small figure walking next to "Saving" in the bottom right corner,
// shown for every save the game writes. Its document, its settings and the pause menu's option.
inline constexpr std::string_view SaveIndicatorDocument = "save_indicator";
// The option: a UI setting and the pause menu's command with the same name (true or false), on by
// default, kept in the user settings.
inline constexpr std::string_view SaveNoticeSetting = "save_notice";
// What the document reads: how opaque the indicator is (0 to 1), how far above its corner it goes
// to stay clear of the windows there (pixels), and whether the figure is drawn (its model loaded).
inline constexpr std::string_view SaveIndicatorOpacitySetting = "save_indicator.opacity";
inline constexpr std::string_view SaveIndicatorLiftSetting = "save_indicator.lift";
inline constexpr std::string_view SaveIndicatorFigureSetting = "save_indicator.figure";
// The autosave option, beside the notice's: a UI setting and the command of its row in the
// options (the game's and the title's), true or false, on by default, kept in the user settings
// (UserSettings::autosave). Off, the session never autosaves (GameSession::setAutosaveEnabled).
inline constexpr std::string_view AutosaveSetting = "autosave";

struct UserSettings;

// The autosave option's command: publishes the new value and saves it with the user settings
// (when `settingsFile` names them). Returns it; nothing when the command's value is not a bool.
std::optional<bool> changeAutosave(const ui::UiValue& value, ui::Ui& ui, UserSettings& settings,
                                   const std::optional<std::filesystem::path>& settingsFile);

// Whether the player sees it now, for routes.
inline constexpr std::string_view SaveIndicatorShownSetting = "save_indicator.shown";
// The text key it shows: "Saving", or that an autosave was skipped for want of a free slot
// (GameSession::autosavesSkipped), which it shows alone, without the figure.
inline constexpr std::string_view SaveIndicatorTextSetting = "save_indicator.text";
inline constexpr std::string_view SavingText = "save.saving";
inline constexpr std::string_view AutosaveNoSlotText = "save.autosave_no_slot";
// The texture the document's figure shows (SaveFigure).
inline constexpr std::string_view SaveFigureTexture = "runtime/save-figure";

// How long the indicator stays: whole from the save's write for HoldSeconds (a save takes a frame,
// too short to read), then fading out over FadeSeconds. A save during it starts it again.
class SaveNotice {
public:
  static constexpr double HoldSeconds = 1.5;
  static constexpr double FadeSeconds = 0.5;

  void start() { left_ = HoldSeconds + FadeSeconds; }
  void advance(double seconds);
  [[nodiscard]] bool active() const { return left_ > 0.0; }
  // 1 while it holds, down to 0 as it fades.
  [[nodiscard]] float opacity() const;

private:
  double left_ = 0.0;
};

// The figure's trot (SaveFigure): back and forth along a short lane, in profile, turning round at
// each end, for as long as the indicator shows. Each lap walks or runs at the partner's speed for
// that gait; the gaits mostly alternate, now and then one repeats (never twice running), all from
// the lap count, so the trot is the same every time and draws on no random numbers. In the
// model's units, x along the lane from its middle.
class LaneRunner {
public:
  enum class Gait : std::uint8_t { Walk, Run };

  // The first partner's speeds, the root motion of its walk and run (field rules: 452 and 870 a
  // second), so the legs match the ground.
  static constexpr float WalkSpeed = 452.0F;
  static constexpr float RunSpeed = 870.0F;
  // How long a turn at an end takes, standing.
  static constexpr float TurnSeconds = 0.2F;

  // The gait of lap `lap` (0 the first).
  [[nodiscard]] static Gait gaitOfLap(std::uint32_t lap);
  [[nodiscard]] static float speedOf(Gait gait) { return gait == Gait::Run ? RunSpeed : WalkSpeed; }

  // A lane from -halfLength to halfLength, starting at its left end on its first lap, rightwards.
  explicit LaneRunner(float halfLength) : halfLength_(halfLength), x_(-halfLength) {}

  void advance(float seconds);
  [[nodiscard]] float x() const { return x_; }
  // +1 going right, -1 going left (the way it faces once a turn is over).
  [[nodiscard]] float direction() const { return direction_; }
  // Its turn round the vertical, in radians: pi/2 facing right, -pi/2 facing left, sweeping
  // between them through facing the viewer during a turn.
  [[nodiscard]] float yaw() const;
  [[nodiscard]] bool turning() const { return turnLeft_ > 0.0F; }
  // The lap it is on, counting from 0; a new lap (and its gait) starts as a turn starts.
  [[nodiscard]] std::uint32_t lap() const { return lap_; }
  [[nodiscard]] Gait gait() const { return gait_; }

private:
  float halfLength_;
  float x_;
  float direction_ = 1.0F;
  float turnLeft_ = 0.0F;
  std::uint32_t lap_ = 0;
  Gait gait_ = Gait::Run;
  // Whether this lap kept the last one's gait.
  bool kept_ = false;
};

// How far up from its corner (`atCorner`, its rectangle there) the indicator must go to be clear of
// every rectangle in `drawn` by `gap` pixels, the lowest such place; nothing when it would have to
// go above `top`. Each candidate sits just above one of the rectangles.
[[nodiscard]] std::optional<float> liftClearOf(const ui::Ui::Rect& atCorner,
                                               std::span<const ui::Ui::Rect> drawn, float gap,
                                               float top);

// Shows the indicator for each save the session writes, while the option is on. Its document is
// shown for the whole game, transparent between saves, so it is always laid out and showing it
// never takes the focus from a menu.
class SaveIndicator {
public:
  SaveIndicator(ui::Ui& ui, std::uint64_t savesWritten, bool enabled);
  SaveIndicator(const SaveIndicator&) = delete;
  SaveIndicator& operator=(const SaveIndicator&) = delete;
  SaveIndicator(SaveIndicator&&) = delete;
  SaveIndicator& operator=(SaveIndicator&&) = delete;
  ~SaveIndicator();

  void setEnabled(bool enabled);
  [[nodiscard]] bool enabled() const { return enabled_; }
  // Once a frame, with the session's count of saves written and the frame's seconds: starts the
  // notice on a new save, moves it on, and places it clear of the windows on screen (the last
  // frame's layout), or hides it while no place on its side is clear.
  void update(std::uint64_t savesWritten, double seconds);
  // Before update, with the session's count of autosaves skipped for want of a free slot: a new one
  // shows the notice that says so.
  void noteSkippedAutosaves(std::uint64_t autosavesSkipped);
  // Whether the indicator is on screen, so its figure must be drawn this frame.
  [[nodiscard]] bool shown() const { return shown_; }

private:
  void publish(float opacity, float lift);
  void showText(std::string_view key);

  ui::Ui* ui_;
  std::uint64_t savesSeen_;
  std::optional<std::uint64_t> skipsSeen_;
  std::string_view text_;
  bool enabled_;
  SaveNotice notice_;
  bool shown_ = false;
  bool placed_ = false;
  float lift_ = 0.0F;
  float publishedOpacity_ = -1.0F;
  float publishedLift_ = -1.0F;
};

} // namespace dwe::app
