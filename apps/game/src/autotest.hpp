#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>
#include <dwe/game/story/story_state.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/platform/events.hpp>
#include <dwe/platform/platform.hpp>
#include <dwe/platform/window.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/story_scenes.hpp>
#include <dwe/ui/ui.hpp>

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "field_scene.hpp"
#include "frame_stats.hpp"

namespace dwe::app {

// A scripted run of the game (`dwe-game --autotest <route.yaml>`, docs/guides/prueba-vertical.md):
// the steps of a route drive the real game through the same events a player makes. Keys and mouse
// events are added to the platform's events, and gamepads are virtual ones of the platform, so the
// input mapping, the UI and the field all run as they do for a player.

enum class AutotestDevice : std::uint8_t { Keyboard, Gamepad, Mouse };

// What a wait step waits for; every condition given must hold.
struct AutotestCondition {
  std::optional<std::string> map;
  std::optional<float> partnerWithin;
  std::optional<glm::vec2> playerNear;
  float playerRadius = 100.0F;
  std::optional<bool> menuOpen;
  // A document of the UI shown or hidden (shown: battle, hidden: battle).
  std::optional<std::string> shown;
  std::optional<std::string> hidden;
  // A map change under way that shows this map's name (its id): the only condition checked during a
  // map change.
  std::optional<std::string> mapNameShown;
  // A condition of the partner, by the status page's key (partner_has:
  // partner_menu.condition.sleepy).
  std::optional<std::string> partnerHas;
  // A setting with this value (setting: {battle.phase: end}), as expect_setting reads it.
  std::optional<std::pair<std::string, std::string>> setting;
  // The model the partner is drawn with (partner_model: digimon/species_b/model), as
  // FieldScene::partnerModel gives it.
  std::optional<std::string> partnerModel;
};

struct AutotestStep {
  enum class Kind : std::uint8_t {
    Wait,          // wait: {seconds} or conditions, with a timeout
    WalkTo,        // walk_to: {trigger | point, radius, walk}: walk there with the device
    Menu,          // menu: open | close, with the device
    MenuSelect,    // menu_select: <element id>: focus and activate an entry of the menu, with the
                   // device
    ExpectSetting, // expect_setting: {name: value}: a setting the game publishes to the UI
    Screenshot,    // screenshot: <name>
    Resize,        // resize: [width, height]
    Fullscreen,    // fullscreen: true | false
    Press, // press: <control>, release: <control>, tap: <control> (key or gamepad button names)
    Release,
    Tap,
    Axis,     // axis: {axis, value}: a stick or trigger of the virtual gamepad
    Click,    // click: [x, z]: a mouse click on a ground point; click: {element: <document>#<id>}
              // on the centre of a UI element; click: {window: [x, y]} on a window pixel
    Script,   // script: <lua>: runs a line of the game's scripts, as the console does (battle("0"))
    Remember, // remember: <name>: keeps a setting's value for a later expect_change
    ExpectChange, // expect_change: {name: up | down | changed}: the setting moved from the
                  // remembered value
    ScriptIf, // script_if: {script, and wait's conditions}: runs the script only if they all hold
              // now
    Hold,     // hold: {keys: [<key>...], ticks}: exactly these keys held for that many logic
              // ticks; back-to-back hold steps follow each other tick for tick (feedTick)
    When,     // when: {wait's conditions, steps}: its steps (the ones after it) run only if the
              // conditions all hold now
    ExpectTextFits, // expect_text_fits: <document> | all: every text the player sees in it fits its
                    // box (Ui::overflowingTexts); a text that does not fails the route at its end,
                    // after the remaining steps have checked theirs
    ExpectClear,    // expect_clear: <document>: nothing the player sees in it (a text, a box, an
                    // image) lies over what another shown document draws (Ui::drawnRects), as
                    // the save indicator; failing as expect_text_fits. <document>#<element>: that
                    // element is shown and lies over nothing else, in its document or another
                    // (Ui::drawnRectsAround), as the HUD's time in digits
    Hover,          // hover: as click's element and window forms, the pointer moved there with no
                    // button
    ExpectRange,    // expect_range: {name: [low, high]}: a setting's number lies in the range,
                    // ends included (the training check's bounds)
  };
  Kind kind = Kind::Wait;
  std::string text; // what the step is about: a control, an element, a name, a setting's value
  std::string name; // a setting's name
  double seconds = 0.0;
  // wait: logic ticks to wait, besides the seconds (game time, which a slow frame drops).
  std::uint64_t ticks = 0;
  double timeout = 10.0;
  AutotestCondition until;
  std::optional<std::int32_t> trigger;
  std::optional<glm::vec2> point;
  // click and hover: `point` is a window pixel, not a ground point (`text` names a UI element
  // instead when it is set).
  bool windowPoint = false;
  float radius = 60.0F;
  bool walk = false;
  // walk_to a trigger: done when its warp starts the map change, without waiting for the new map.
  bool stopOnWarp = false;
  // walk_to: done when a story script takes hold of the player on the way (a step cell's scene, or
  // a tournament's board or match), with the scene left running for the next steps.
  bool stopOnScene = false;
  // walk_to a trigger: done when the story lock keeps its exit closed and its notice shows.
  bool stopOnClosed = false;
  // walk_to: an encounter on the way (a script holds the player, or a fight) is waited out.
  bool encounters = false;
  // walk_to with `encounters: win`: the encounter's fight ends with the enemies beaten (the
  // battle_win command) once it starts, so a route's partner never loses on the way.
  bool winEncounters = false;
  // wait: while it waits, the story's textbox pages and a fight's end box are confirmed, and a
  // fight plays out on its own (a long scene played through).
  bool confirm = false;
  // wait with confirm and `answer: true`: a selection is answered too, with the entry its cursor
  // is on (a long run of the same choice, such as a shop job's customers).
  bool answer = false;
  // wait with confirm: a fight on screen for this many seconds is ended
  // with battle_win(""), as the debug command does (a route that checks a path, not the fight).
  std::optional<double> winAfter;
  bool flag = false; // menu open, fullscreen on
  // when: how many of the steps after it are its own, skipped when its conditions do not hold.
  std::size_t block = 0;
  int width = 0;
  int height = 0;
  float value = 0.0F;
  // expect_range: the bounds, ends included.
  double low = 0.0;
  double high = 0.0;
  std::optional<AutotestDevice> device;
  // hold: the keys held, by name (keyNamed).
  std::vector<std::string> keys;
};

struct AutotestRoute {
  std::string name;
  AutotestDevice device = AutotestDevice::Keyboard;
  // The family of the virtual gamepad, for gamepad routes.
  platform::GamepadFamily gamepadFamily = platform::GamepadFamily::Xbox;
  double timeout = 600.0;
  // Whether the route starts at the main menu; without it the game goes straight to the field.
  bool mainMenu = false;
  // Whether a route that starts at the main menu starts at the screen before it, where the
  // original look says "Press Start" (boot_screen); the game itself always shows it.
  bool bootScreen = false;
  // Whether that screen comes after the language box, as the game shows it (language_first).
  bool languageFirst = false;
  // Whether a new game from the main menu shows its rules screen (ui/new_game_rules) for the route
  // to answer (new_game_rules); otherwise it starts with the command line's ruleset.
  bool newGameRules = false;
  // Whether a new game from the main menu asks the opening's questions and the names first
  // (new_game_scene), as without a route; otherwise it starts without them.
  bool newGameScene = false;
  // What walks the player (walk_control: keys, mouse; walk_control.hpp): the mouse for a mouse
  // route, the keys otherwise. With the keys, a click on the ground (click: [x, z]) gives the walk
  // order itself, as a shortcut of the route like place().
  std::optional<bool> walksWithMouse;
  // How a new game starts (new_game); without it, on the seeded chain (a free field), so only
  // routes through the opening say original.
  std::optional<session::NewGameChain> newGameChain;
  // The names a new game starts with (names: {player, partner}), as if the opening's naming box had
  // asked for them; an autotest skips that box, so without it the names stay unset.
  std::optional<session::NewGameChoice> names;
  // A save file to start from (load_save: <path>, relative to the route), as a bug report's route
  // does: the game loads it instead of starting a new game, and the field opens where it was saved.
  std::optional<std::filesystem::path> loadSave;
  // Whether the game's movies play as without a route (movies: true), for a route that sees the
  // ending through to its credits; without it, a moment's movie counts as played at once.
  bool movies = false;
  // Whether each screenshot also checks that every text the player sees fits its box (text_fit:
  // true), as expect_text_fits: all at that moment, for a route made for something else.
  bool textFit = false;
  std::vector<AutotestStep> steps;
};

// Reads a route (the format is in docs/guides/prueba-vertical.md). Errors name the line and column.
core::Result<AutotestRoute> parseAutotestRoute(std::string_view yaml);

// What the driver reads and drives each frame. Everything but the scene outlives the driver.
struct AutotestHost {
  platform::Platform* platform = nullptr;
  platform::Window* window = nullptr;
  render::Renderer* renderer = nullptr;
  rhi::Device* device = nullptr;
  // The field of the game being played; nothing at the title screen.
  const FieldScene* scene = nullptr;
  const ui::Ui* ui = nullptr;
  const input::InputState* input = nullptr;
  FrameStats* stats = nullptr;
  script::ScriptVm* scripts = nullptr;
  // The partner's status as the status page shows it (partner.* settings), refreshed every frame,
  // menu open or not.
  const screens::ScreenSettings* partner = nullptr;
  // The story's triggers and variables (story.trigger.<n> and story.var.<n> settings); nothing
  // at the title screen.
  const game::story::StoryState* story = nullptr;
  // The saves folder (--saves), whose files a route counts (saves.files); nothing without one.
  std::optional<std::filesystem::path> saves;
};

class Autotest {
public:
  // Reads the route; screenshots go to `shots` when given, and are skipped otherwise. Steps of the
  // kinds in `skipKinds` (as written in routes, such as fullscreen) are skipped, and the report
  // says so.
  static core::Result<std::unique_ptr<Autotest>> load(const std::filesystem::path& route,
                                                      std::optional<std::filesystem::path> shots,
                                                      std::vector<std::string> skipKinds);

  Autotest(const Autotest&) = delete;
  Autotest& operator=(const Autotest&) = delete;
  Autotest(Autotest&&) = delete;
  Autotest& operator=(Autotest&&) = delete;
  ~Autotest();

  // Runs the route for one frame, before the platform's events are polled: appends this frame's
  // keyboard and mouse events to `events` and moves the virtual gamepad. `ticks` counts the logic
  // ticks run so far.
  void frame(const AutotestHost& host, std::uint64_t ticks, double now,
             std::vector<platform::Event>& events);

  // Feeds the input the key changes the hold steps ask for at this logic tick, inside the tick
  // loop, so a hold lasts its ticks exactly however long the frames take.
  void feedTick(std::uint64_t tick, input::InputState& input);

  // Notes a map change for the report.
  void mapChanged(const MapChangeTiming& timing);

  // The logic ticks a frame runs, out of the `ticks` its time gives: a walk_to with the keys or
  // the stick steers once a frame, so it caps them at SteeredTicks. A slow frame (a software
  // renderer draws a few frames a second) would otherwise run its whole quarter second on the last
  // frame's keys, and a run overshoots a turn of the path into a warp cell beside it (mgen02's way
  // back down next to the arrival from below). The game's own ticks are fixed, so this only slows
  // the route's clock.
  [[nodiscard]] int ticksThisFrame(int ticks) const;

  [[nodiscard]] bool finished() const { return result_.has_value(); }

  // Whether the route starts at the main menu (AutotestRoute::mainMenu).
  [[nodiscard]] bool startsAtMainMenu() const { return route_.mainMenu; }
  // Whether it starts at the screen before the main menu (AutotestRoute::bootScreen).
  [[nodiscard]] bool startsAtBootScreen() const { return route_.bootScreen; }
  // Whether the language box comes before that screen (AutotestRoute::languageFirst).
  [[nodiscard]] bool asksLanguageFirst() const { return route_.languageFirst; }
  // Whether a new game shows its rules screen (AutotestRoute::newGameRules).
  [[nodiscard]] bool asksNewGameRules() const { return route_.newGameRules; }
  // Whether a new game asks the opening's questions (AutotestRoute::newGameScene).
  [[nodiscard]] bool asksNewGameScene() const { return route_.newGameScene; }
  // Whether the mouse walks the player (AutotestRoute::walksWithMouse).
  [[nodiscard]] bool walksWithMouse() const {
    return route_.walksWithMouse.value_or(route_.device == AutotestDevice::Mouse);
  }
  // The ground point of a click step the keys' walk control does not take as a click, once.
  std::optional<glm::vec3> takeWalkOrder() { return std::exchange(walkOrder_, std::nullopt); }
  // Whether the game's movies play (AutotestRoute::movies).
  [[nodiscard]] bool playsMovies() const { return route_.movies; }

  // How the route's new game starts (AutotestRoute::newGameChain), if it says.
  [[nodiscard]] std::optional<session::NewGameChain> newGameChain() const {
    return route_.newGameChain;
  }

  // The names the route's new game starts with (AutotestRoute::names), if it says.
  [[nodiscard]] const std::optional<session::NewGameChoice>& newGameNames() const {
    return route_.names;
  }

  // The save the route starts from (AutotestRoute::loadSave), relative paths already made relative
  // to the route's folder.
  [[nodiscard]] const std::optional<std::filesystem::path>& loadSave() const {
    return route_.loadSave;
  }

  // Logs what the run measured (steps, map changes, frame times), while the game is still up.
  void summarize(const AutotestHost& host) const;

  // Judges the run once the game has shut down, so that what teardown reports counts too. Logs the
  // validation layer's state and messages and how many [error] lines the log had, and gives the
  // exit code: 0 when every step passed with no error and no validation message, 2 when it passed
  // but walk_to or wait steps were skipped, 1 otherwise.
  [[nodiscard]] int verdict(bool validationActive) const;

  // Stops the run with a failure, for problems the driver cannot see (the window closed).
  void fail(std::string message);

private:
  // An input event or gamepad move, run once enough logic ticks have passed since the previous one.
  struct Action {
    int afterTicks = 0;
    std::function<void(std::vector<platform::Event>&)> run;
  };
  // The state of the step being run.
  struct Progress;

  Autotest(AutotestRoute route, std::optional<std::filesystem::path> shots,
           std::vector<std::string> skipKinds);

  [[nodiscard]] AutotestDevice deviceOf(const AutotestStep& step) const;
  void runActions(std::uint64_t ticks, std::vector<platform::Event>& events);
  // Runs the current step; true when it is done.
  bool runStep(const AutotestHost& host, const AutotestStep& step, double now);
  bool waitStep(const AutotestHost& host, const AutotestStep& step, double now);
  bool walkStep(const AutotestHost& host, const AutotestStep& step, double now);
  void walkWithMouse(const AutotestHost& host, double now);
  bool menuStep(const AutotestHost& host, const AutotestStep& step, double now);
  bool menuSelectStep(const AutotestHost& host, const AutotestStep& step);
  bool screenshotStep(const AutotestHost& host, const AutotestStep& step);
  bool resizeStep(const AutotestHost& host, const AutotestStep& step, double now);
  bool controlStep(const AutotestHost& host, const AutotestStep& step);
  bool clickStep(const AutotestHost& host, const AutotestStep& step);
  bool scriptStep(const AutotestHost& host, const AutotestStep& step);
  bool scriptIfStep(const AutotestHost& host, const AutotestStep& step);
  bool whenStep(const AutotestHost& host, const AutotestStep& step);
  bool textFitStep(const AutotestHost& host, const AutotestStep& step, double now);
  bool clearStep(const AutotestHost& host, const AutotestStep& step, double now);
  // The texts of `document` (or all) that do not fit now, logged and counted as a failure.
  void reportTextOverflows(std::vector<ui::Ui::TextOverflow> overflows);
  // What of the condition does not hold now, or empty if all of it does.
  [[nodiscard]] static std::string unmetCondition(const AutotestHost& host,
                                                  const AutotestCondition& until);
  // The same for the parts of the condition on the UI, which also hold at the title screen.
  [[nodiscard]] static std::string unmetInUi(const AutotestHost& host,
                                             const AutotestCondition& until);
  bool rememberStep(const AutotestHost& host, const AutotestStep& step);
  bool expectChangeStep(const AutotestHost& host, const AutotestStep& step);
  // Plays the current hold step; the first of a run of them schedules the whole run.
  bool holdStep();

  // Device helpers that queue actions.
  void key(platform::KeyCode code, bool down, int afterTicks = 0);
  void button(platform::GamepadButton pressed, bool down, int afterTicks = 0);
  void axis(platform::GamepadAxis moved, float value);
  // Presses and releases an action's control on a device: menu, confirm, cancel, up, down.
  void tapAction(AutotestDevice device, std::string_view action);
  // win_after: ends a fight that has lasted the step's seconds with battle_win("").
  void winLongFight(const AutotestHost& host, const AutotestStep& step, double now);
  void clickAt(glm::vec2 window, platform::MouseButton pressed = platform::MouseButton::Left);
  // Holds the left button down at a window point for a number of logic ticks: the player walks
  // towards it.
  void holdAt(glm::vec2 window, int ticks);
  void releaseMovement();
  void steer(AutotestDevice device, glm::vec2 stick, bool walk);
  // Plugs in the route's virtual gamepad; true once the input sees it.
  bool ensureGamepad(const AutotestHost& host);

  void finishStep(double now);
  void failStep(std::string_view problem);

  AutotestRoute route_;
  std::optional<glm::vec3> walkOrder_;
  std::optional<std::filesystem::path> shots_;
  std::vector<std::string> skipKinds_;
  std::optional<std::string> result_; // empty on success, else the failure
  // The expect_text_fits steps that found texts sticking out, for the route's result at its end.
  std::vector<std::string> textFailures_;
  std::size_t stepIndex_ = 0;
  std::unique_ptr<Progress> progress_;
  // The most logic ticks a frame runs while a walk steers (ticksThisFrame): 15 turns a second.
  static constexpr int SteeredTicks = 4;
  double runStart_ = -1.0;
  std::deque<Action> actions_;
  std::uint64_t lastActionTick_ = 0;
  // The logic ticks run so far, as frame() last heard.
  std::uint64_t ticks_ = 0;
  // The keys of a run of hold steps, by the logic tick they start at and end before, and the end
  // tick of each of its steps by step index.
  struct ScheduledHold {
    std::uint64_t from = 0;
    std::uint64_t to = 0;
    std::vector<platform::KeyCode> keys;
  };
  std::vector<ScheduledHold> holds_;
  std::map<std::size_t, std::uint64_t> holdEnds_;
  // The keys feedTick holds now.
  std::vector<platform::KeyCode> tickKeys_;
  platform::Platform* platform_ = nullptr;
  std::optional<platform::GamepadId> gamepad_;
  // Keys and buttons held by the driver, to release them.
  std::vector<platform::KeyCode> keysDown_;
  // The gamepad's walk button (West) is held.
  bool walkButtonDown_ = false;
  glm::vec2 stick_{0.0F};
  std::vector<std::string> stepLog_;
  // Steps the screen could not run (a window larger than the desktop), for the report.
  std::vector<std::string> skipped_;
  std::vector<MapChangeTiming> mapChanges_;
  int screenshots_ = 0;
  // The values remember steps kept, by setting name.
  std::map<std::string, std::string, std::less<>> remembered_;
  // The [error] lines logged since the route was loaded; shared with the log sink, which outlives
  // the driver.
  std::shared_ptr<std::atomic<std::uint64_t>> errors_ =
      std::make_shared<std::atomic<std::uint64_t>>(0);
};

} // namespace dwe::app
