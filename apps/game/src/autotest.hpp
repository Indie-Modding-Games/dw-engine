#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/math.hpp>
#include <dwe/input/input_state.hpp>
#include <dwe/platform/events.hpp>
#include <dwe/platform/platform.hpp>
#include <dwe/platform/window.hpp>
#include <dwe/render/renderer.hpp>
#include <dwe/screens/screen_settings.hpp>
#include <dwe/script/script_vm.hpp>
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
    Click,    // click: [x, z]: a mouse click on a ground point
    Script,   // script: <lua>: runs a line of the game's scripts, as the console does (battle("0"))
    Remember, // remember: <name>: keeps a setting's value for a later expect_change
    ExpectChange, // expect_change: {name: up | down | changed}: the setting moved from the
                  // remembered value
    ScriptIf, // script_if: {script, and wait's conditions}: runs the script only if they all hold
              // now
  };
  Kind kind = Kind::Wait;
  std::string text; // what the step is about: a control, an element, a name, a setting's value
  std::string name; // a setting's name
  double seconds = 0.0;
  double timeout = 10.0;
  AutotestCondition until;
  std::optional<std::int32_t> trigger;
  std::optional<glm::vec2> point;
  float radius = 60.0F;
  bool walk = false;
  // walk_to a trigger: done when its warp starts the map change, without waiting for the new map.
  bool stopOnWarp = false;
  bool flag = false; // menu open, fullscreen on
  int width = 0;
  int height = 0;
  float value = 0.0F;
  std::optional<AutotestDevice> device;
};

struct AutotestRoute {
  std::string name;
  AutotestDevice device = AutotestDevice::Keyboard;
  // The family of the virtual gamepad, for gamepad routes.
  platform::GamepadFamily gamepadFamily = platform::GamepadFamily::Xbox;
  double timeout = 600.0;
  // Whether the route starts at the main menu; without it the game goes straight to the field.
  bool mainMenu = false;
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

  // Notes a map change for the report.
  void mapChanged(const MapChangeTiming& timing);

  [[nodiscard]] bool finished() const { return result_.has_value(); }

  // Whether the route starts at the main menu (AutotestRoute::mainMenu).
  [[nodiscard]] bool startsAtMainMenu() const { return route_.mainMenu; }

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
  // What of the condition does not hold now, or empty if all of it does.
  [[nodiscard]] static std::string unmetCondition(const AutotestHost& host,
                                                  const AutotestCondition& until);
  // The same for the parts of the condition on the UI, which also hold at the title screen.
  [[nodiscard]] static std::string unmetInUi(const AutotestHost& host,
                                             const AutotestCondition& until);
  bool rememberStep(const AutotestHost& host, const AutotestStep& step);
  bool expectChangeStep(const AutotestHost& host, const AutotestStep& step);

  // Device helpers that queue actions.
  void key(platform::KeyCode code, bool down, int afterTicks = 0);
  void button(platform::GamepadButton pressed, bool down, int afterTicks = 0);
  void axis(platform::GamepadAxis moved, float value);
  // Presses and releases an action's control on a device: menu, confirm, cancel, up, down.
  void tapAction(AutotestDevice device, std::string_view action);
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
  std::optional<std::filesystem::path> shots_;
  std::vector<std::string> skipKinds_;
  std::optional<std::string> result_; // empty on success, else the failure
  std::size_t stepIndex_ = 0;
  std::unique_ptr<Progress> progress_;
  double runStart_ = -1.0;
  std::deque<Action> actions_;
  std::uint64_t lastActionTick_ = 0;
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
