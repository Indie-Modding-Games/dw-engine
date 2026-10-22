#include "autotest.hpp"

#include <dwe/assets/image.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/field/grid.hpp>
#include <dwe/input/keys.hpp>
#include <dwe/rhi/device.hpp>
#include <dwe/screens/partner_status_view.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

#include "common.hpp"
#include "device_names.hpp"
#include "messages.hpp"

namespace dwe::app {
namespace {

using Kind = AutotestStep::Kind;

// The stick aims at the next waypoint of the path once this close to the current one: about a tick
// of running.
constexpr float WaypointReach = 20.0F;
// Eight key directions follow a line less closely than a stick.
constexpr float KeyWaypointReach = 40.0F;
// How long a stuck walk pushes at an angle to its aim.
constexpr double UnstickSeconds = 0.5;
// A walk that moves less than this in StallSeconds plans its path again.
constexpr float ProgressDistance = 30.0F;
constexpr double StallSeconds = 1.5;
// Keys turn a direction into one of eight: a component counts past sin(22.5 degrees).
constexpr float KeyThreshold = 0.383F;
// How long the driver holds a tapped control, and waits after it, in logic ticks.
constexpr int TapTicks = 3;
// A click-to-move point must be this far inside the window (a fraction of its smaller side).
constexpr float ClickMargin = 0.02F;
// Frames after a resize, a screenshot or a mode change that the steady frame times leave out.
constexpr int SettleFrames = 10;
// The exit code of a run that passed with walk_to or wait steps skipped by --skip-steps.
constexpr int PartialExitCode = 2;
// Frames the window size must keep before a resize counts as done.
constexpr int StableFrames = 5;

// The other documents menu_select picks from while they are open: the battle screen, the partner's
// menu, free exploration's chooser and the title screen's slot list and dialog.
constexpr std::array<std::string_view, 5> ScreenDocuments{
    "partner_menu", "battle", "destination_choice", "dialog", "save_slots"};

// The menu menu_select picks from: the main menu while it is open, then the pause menu, then the
// battle screen or the partner's menu; the pause menu when none is open, so the step says so.
std::string_view shownMenu(const ui::Ui& ui) {
  if (ui.isVisible(MainMenuDocument)) {
    return MainMenuDocument;
  }
  if (ui.isVisible(MenuDocument)) {
    return MenuDocument;
  }
  for (const std::string_view document : ScreenDocuments) {
    if (ui.isVisible(document)) {
      return document;
    }
  }
  return MenuDocument;
}

std::optional<AutotestDevice> deviceNamed(std::string_view name) {
  if (name == "keyboard") {
    return AutotestDevice::Keyboard;
  }
  if (name == "gamepad") {
    return AutotestDevice::Gamepad;
  }
  if (name == "mouse") {
    return AutotestDevice::Mouse;
  }
  return std::nullopt;
}

std::string_view deviceName(AutotestDevice device) {
  switch (device) {
  case AutotestDevice::Keyboard:
    return "keyboard";
  case AutotestDevice::Gamepad:
    return "gamepad";
  case AutotestDevice::Mouse:
    return "mouse";
  }
  return "?";
}

// The steps' names in routes, in the order of AutotestStep::Kind.
constexpr std::array<std::string_view, 17> StepNames{
    "wait",          "walk_to",  "menu",       "menu_select", "expect_setting",
    "screenshot",    "resize",   "fullscreen", "press",       "release",
    "tap",           "axis",     "click",      "script",      "remember",
    "expect_change", "script_if"};

// What an expect_change step asks of a setting against its remembered value.
constexpr std::string_view ChangedUp = "up";
constexpr std::string_view ChangedDown = "down";
constexpr std::string_view ChangedAtAll = "changed";
// The key of a block of steps repeated in place (repeat: {times, steps}).
constexpr std::string_view RepeatKey = "repeat";
// The key of a step's mapping that is its timeout, beside the setting it names.
constexpr std::string_view TimeoutKey = "timeout";

std::string_view kindName(Kind kind) {
  return StepNames.at(static_cast<std::size_t>(kind));
}

std::string describe(const ui::UiValue& value) {
  if (const auto* flag = std::get_if<bool>(&value)) {
    return *flag ? "true" : "false";
  }
  if (const auto* number = std::get_if<double>(&value)) {
    return std::format("{}", *number);
  }
  if (const auto* text = std::get_if<std::string>(&value)) {
    return *text;
  }
  return "-";
}

glm::vec2 readPoint(assets::YamlReader& reader, const core::YamlNode& node, std::string_view what) {
  const auto items = reader.sequence(node, what, 2);
  if (items.size() != 2) {
    return glm::vec2{0.0F};
  }
  return {reader.number(items[0]), reader.number(items[1])};
}

// A step's {name: value} mapping, with an optional timeout key: the setting it names and its value.
std::optional<std::pair<std::string, std::string>> readSetting(assets::YamlReader& reader,
                                                               const core::YamlNode& node,
                                                               AutotestStep& step,
                                                               std::string_view what) {
  std::optional<std::pair<std::string, std::string>> setting;
  if (node.isMapping()) {
    for (const core::YamlEntry& entry : node.entries()) {
      if (entry.key == TimeoutKey) {
        step.timeout = reader.number(entry.value);
      } else if (!setting) {
        setting.emplace(entry.key, reader.text(entry.value));
      } else {
        setting.reset();
        break;
      }
    }
  }
  if (!setting) {
    reader.fail(node, std::format("{} is {{name: value}}, with an optional timeout", what));
  }
  return setting;
}

// The value of a setting as text: the partner's live status for partner.* settings, the UI's
// published ones otherwise.
std::string settingText(const AutotestHost& host, std::string_view name) {
  if (host.partner != nullptr) {
    const auto found = std::ranges::find(*host.partner, name, &screens::ScreenSetting::name);
    if (found != host.partner->end()) {
      return describe(found->value);
    }
  }
  return describe(host.ui->setting(name));
}

// Whether the partner's live status lists a condition, by its key.
bool partnerHas(const AutotestHost& host, std::string_view key) {
  if (host.partner == nullptr) {
    return false;
  }
  return std::ranges::any_of(*host.partner, [key](const screens::ScreenSetting& setting) {
    const auto* text = std::get_if<std::string>(&setting.value);
    return setting.name.starts_with(screens::PartnerConditionList) && text != nullptr &&
           *text == key;
  });
}

// Whether `now` moved from `before` as an expect_change step asks: up, down or changed at all.
bool movedAsAsked(std::string_view before, std::string_view now, std::string_view asked) {
  if (asked == ChangedAtAll) {
    return before != now;
  }
  double from = 0.0;
  double to = 0.0;
  const auto [fromEnd, fromError] =
      std::from_chars(before.data(), before.data() + before.size(), from);
  const auto [toEnd, toError] = std::from_chars(now.data(), now.data() + now.size(), to);
  if (fromError != std::errc{} || toError != std::errc{}) {
    return false;
  }
  return asked == ChangedUp ? to > from : to < from;
}

void readCondition(assets::YamlReader& reader, const core::YamlNode& node, AutotestStep& step) {
  if (const core::YamlNode* map = node.find("map")) {
    step.until.map = reader.text(*map);
  }
  if (const core::YamlNode* within = node.find("partner_within")) {
    step.until.partnerWithin = reader.number(*within);
  }
  if (const core::YamlNode* near = node.find("player_near")) {
    step.until.playerNear = readPoint(reader, *near, "player_near");
  }
  if (const core::YamlNode* radius = node.find("radius")) {
    step.until.playerRadius = reader.number(*radius);
  }
  if (const core::YamlNode* open = node.find("menu_open")) {
    step.until.menuOpen = reader.boolean(*open);
  }
  if (const core::YamlNode* named = node.find("map_name")) {
    step.until.mapNameShown = reader.text(*named);
  }
  if (const core::YamlNode* shown = node.find("shown")) {
    step.until.shown = reader.text(*shown);
  }
  if (const core::YamlNode* hidden = node.find("hidden")) {
    step.until.hidden = reader.text(*hidden);
  }
  if (const core::YamlNode* has = node.find("partner_has")) {
    step.until.partnerHas = reader.text(*has);
  }
  if (const core::YamlNode* setting = node.find("setting")) {
    step.until.setting = readSetting(reader, *setting, step, "setting");
  }
  if (const core::YamlNode* model = node.find("partner_model")) {
    step.until.partnerModel = reader.text(*model);
  }
  if (const core::YamlNode* seconds = node.find("seconds")) {
    step.seconds = reader.number(*seconds);
  }
}

void readCommon(assets::YamlReader& reader, const core::YamlNode& node, AutotestStep& step) {
  if (const core::YamlNode* timeout = node.find("timeout")) {
    step.timeout = reader.number(*timeout);
  }
  if (const core::YamlNode* device = node.find("device")) {
    const std::string name = reader.text(*device);
    step.device = deviceNamed(name);
    if (!step.device) {
      reader.fail(*device, "the device must be keyboard, gamepad or mouse");
    }
  }
}

AutotestStep readStep(assets::YamlReader& reader, const core::YamlNode& node);

// A list of steps, where repeat: {times, steps} stands for its steps that many times over.
void readSteps(assets::YamlReader& reader, const core::YamlNode& node,
               std::vector<AutotestStep>& steps) {
  for (const core::YamlNode& step : reader.sequence(node, "steps")) {
    const core::YamlNode* repeat = step.isMapping() ? step.find(RepeatKey) : nullptr;
    if (repeat == nullptr) {
      steps.push_back(readStep(reader, step));
      continue;
    }
    reader.expectKeys(*repeat, RepeatKey, {"times", "steps"}, {});
    const auto times = reader.integer<std::int32_t>(reader.field(*repeat, "times"));
    if (times <= 0) {
      reader.fail(*repeat, "repeat takes a positive times");
    }
    std::vector<AutotestStep> block;
    readSteps(reader, reader.field(*repeat, "steps"), block);
    for (std::int32_t round = 0; round < times; ++round) {
      steps.insert(steps.end(), block.begin(), block.end());
    }
  }
}

// One step: a mapping with a single key, the kind of step, whose value says the rest.
AutotestStep readStep(assets::YamlReader& reader, const core::YamlNode& node) {
  AutotestStep step;
  if (!node.isMapping() || node.entries().size() != 1) {
    reader.fail(node, "a step is a mapping with one key, its kind");
    return step;
  }
  const std::span<const core::YamlEntry> entries = node.entries();
  const std::string& kind = entries.front().key;
  const core::YamlNode& value = entries.front().value;
  if (kind == "wait") {
    step.kind = Kind::Wait;
    reader.expectKeys(value, "wait", {},
                      {"seconds", "map", "partner_within", "player_near", "radius", "menu_open",
                       "map_name", "shown", "hidden", "partner_has", "setting", "partner_model",
                       "timeout"});
    readCondition(reader, value, step);
    readCommon(reader, value, step);
  } else if (kind == "walk_to") {
    step.kind = Kind::WalkTo;
    step.timeout = 90.0;
    reader.expectKeys(value, "walk_to", {},
                      {"trigger", "point", "radius", "walk", "stop_on_warp", "device", "timeout"});
    if (const core::YamlNode* trigger = value.find("trigger")) {
      step.trigger = reader.integer<std::int32_t>(*trigger);
    }
    if (const core::YamlNode* point = value.find("point")) {
      step.point = readPoint(reader, *point, "point");
    }
    if (step.trigger.has_value() == step.point.has_value()) {
      reader.fail(value, "walk_to takes a trigger or a point");
    }
    if (const core::YamlNode* radius = value.find("radius")) {
      step.radius = reader.number(*radius);
    }
    if (const core::YamlNode* walk = value.find("walk")) {
      step.walk = reader.boolean(*walk);
    }
    if (const core::YamlNode* stop = value.find("stop_on_warp")) {
      step.stopOnWarp = reader.boolean(*stop);
    }
    readCommon(reader, value, step);
  } else if (kind == "menu") {
    step.kind = Kind::Menu;
    const std::string action = reader.text(value);
    if (action != "open" && action != "close") {
      reader.fail(value, "menu is open or close");
    }
    step.flag = action == "open";
  } else if (kind == "script") {
    step.kind = Kind::Script;
    step.text = reader.text(value);
  } else if (kind == "script_if") {
    step.kind = Kind::ScriptIf;
    reader.expectKeys(value, "script_if", {"script"},
                      {"map", "partner_within", "player_near", "radius", "menu_open", "shown",
                       "hidden", "partner_has", "setting"});
    readCondition(reader, value, step);
    if (const core::YamlNode* script = value.find("script")) {
      step.text = reader.text(*script);
    }
  } else if (kind == "menu_select") {
    step.kind = Kind::MenuSelect;
    step.text = reader.text(value);
  } else if (kind == "expect_setting" || kind == "expect_change") {
    step.kind = kind == "expect_setting" ? Kind::ExpectSetting : Kind::ExpectChange;
    step.timeout = 2.0;
    if (const auto setting = readSetting(reader, value, step, kind)) {
      step.name = setting->first;
      step.text = setting->second;
    }
    if (step.kind == Kind::ExpectChange && step.text != ChangedUp && step.text != ChangedDown &&
        step.text != ChangedAtAll) {
      reader.fail(value, "expect_change is {name: up | down | changed}");
    }
  } else if (kind == "remember") {
    step.kind = Kind::Remember;
    step.name = reader.text(value);
  } else if (kind == "screenshot") {
    step.kind = Kind::Screenshot;
    step.text = reader.text(value);
  } else if (kind == "resize") {
    step.kind = Kind::Resize;
    const auto items = reader.sequence(value, "resize", 2);
    if (items.size() == 2) {
      step.width = reader.integer<int>(items[0]);
      step.height = reader.integer<int>(items[1]);
    }
    if (step.width <= 0 || step.height <= 0) {
      reader.fail(value, "resize takes [width, height]");
    }
  } else if (kind == "fullscreen") {
    step.kind = Kind::Fullscreen;
    step.flag = reader.boolean(value);
  } else if (kind == "press" || kind == "release" || kind == "tap") {
    if (kind == "press") {
      step.kind = Kind::Press;
    } else if (kind == "release") {
      step.kind = Kind::Release;
    } else {
      step.kind = Kind::Tap;
    }
    reader.expectKeys(value, kind, {}, {"key", "button"});
    if (const core::YamlNode* key = value.find("key")) {
      step.name = "key";
      step.text = reader.text(*key);
      if (!keyNamed(step.text)) {
        reader.fail(*key, "unknown key");
      }
    } else if (const core::YamlNode* button = value.find("button")) {
      step.name = "button";
      step.text = reader.text(*button);
      if (!gamepadButtonNamed(step.text)) {
        reader.fail(*button, "unknown gamepad button");
      }
    } else {
      reader.fail(value, "a key or a button");
    }
  } else if (kind == "axis") {
    step.kind = Kind::Axis;
    reader.expectKeys(value, "axis", {"axis", "value"});
    step.text = reader.text(reader.field(value, "axis"));
    step.value = reader.number(reader.field(value, "value"));
    if (!gamepadAxisNamed(step.text)) {
      reader.fail(value, "unknown gamepad axis");
    }
  } else if (kind == "click") {
    step.kind = Kind::Click;
    step.point = readPoint(reader, value, "click");
  } else {
    reader.fail(node, std::format("unknown step '{}'", kind));
  }
  return step;
}

// The cells a walk heads for: those with the trigger, or the one under the point.
std::vector<assets::CollisionCell> goalCells(const assets::MapCollision& collision,
                                             const AutotestStep& step) {
  std::vector<assets::CollisionCell> goals;
  if (step.point) {
    goals.push_back(assets::collisionCellAt({step.point->x, 0.0F, step.point->y}));
    return goals;
  }
  for (std::int32_t row = 0; row < assets::MapCollision::Size; ++row) {
    for (std::int32_t column = 0; column < assets::MapCollision::Size; ++column) {
      const assets::CollisionCell cell{.column = column, .row = row};
      if (collision.trigger(cell) == step.trigger) {
        goals.push_back(cell);
      }
    }
  }
  return goals;
}

float groundDistance(glm::vec3 a, glm::vec3 b) {
  return std::hypot(a.x - b.x, a.z - b.z);
}

std::string where(const FieldScene* scene) {
  if (scene == nullptr) {
    return "at the title screen";
  }
  const game::field::Field& field = scene->field();
  const glm::vec3 player = field.player().position;
  return std::format("map {}, player at ({:.0f}, {:.0f}), partner {:.0f} away", field.mapId(),
                     player.x, player.z, groundDistance(player, field.partner().position));
}

} // namespace

// The state of the step being run.
struct Autotest::Progress {
  double start = 0.0;
  bool begun = false;
  int phase = 0;
  int attempts = 0;
  std::uint32_t mapSerial = 0;
  // walk_to
  std::vector<glm::vec3> path;
  glm::vec3 lastPosition{0.0F};
  double lastProgress = 0.0;
  double lastClick = -1.0;
  glm::vec2 unstick{0.0F};
  double unstickUntil = -1.0;
  int clickBackoff = 0;
  bool clickTook = false;
  // menu
  double lastTry = -1.0;
  // resize and fullscreen
  rhi::Extent lastExtent{};
  int stableFrames = 0;
  // screenshot: 0 waiting, 1 written, 2 failed
  std::shared_ptr<int> shot;
  std::shared_ptr<std::string> shotMessage;
};

core::Result<AutotestRoute> parseAutotestRoute(std::string_view yaml) {
  auto document = core::parseYaml(yaml);
  if (!document) {
    return std::unexpected{document.error()};
  }
  assets::YamlReader reader;
  AutotestRoute route;
  reader.expectKeys(*document, "route", {"name", "device", "steps"},
                    {"gamepad", "timeout", "main_menu"});
  route.name = reader.text(reader.field(*document, "name"));
  const core::YamlNode& device = reader.field(*document, "device");
  if (const auto known = deviceNamed(reader.text(device))) {
    route.device = *known;
  } else {
    reader.fail(device, "the device must be keyboard, gamepad or mouse");
  }
  if (const core::YamlNode* gamepad = document->find("gamepad")) {
    if (const auto family = gamepadFamilyNamed(reader.text(*gamepad))) {
      route.gamepadFamily = *family;
    } else {
      reader.fail(*gamepad, "unknown gamepad family");
    }
  }
  if (const core::YamlNode* mainMenu = document->find("main_menu")) {
    route.mainMenu = reader.boolean(*mainMenu);
  }
  if (const core::YamlNode* timeout = document->find("timeout")) {
    route.timeout = reader.number(*timeout);
  }
  readSteps(reader, reader.field(*document, "steps"), route.steps);
  return reader.finish(std::move(route), "autotest route");
}

core::Result<std::unique_ptr<Autotest>> Autotest::load(const std::filesystem::path& route,
                                                       std::optional<std::filesystem::path> shots,
                                                       std::vector<std::string> skipKinds) {
  std::ifstream file{route, std::ios::binary};
  if (!file) {
    return core::fail(std::format("cannot read {}", route.string()));
  }
  const std::string text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
  auto parsed = parseAutotestRoute(text);
  if (!parsed) {
    return core::fail(std::format("{}: {}", route.string(), parsed.error().message));
  }
  if (shots) {
    std::error_code error;
    std::filesystem::create_directories(*shots, error);
    if (error) {
      return core::fail(std::format("cannot make {}: {}", shots->string(), error.message()));
    }
  }
  for (const std::string& kind : skipKinds) {
    if (std::ranges::find(StepNames, kind) == StepNames.end()) {
      return core::fail(std::format("--skip-steps: unknown step '{}'", kind));
    }
  }
  return std::unique_ptr<Autotest>{
      new Autotest{std::move(*parsed), std::move(shots), std::move(skipKinds)}};
}

Autotest::Autotest(AutotestRoute route, std::optional<std::filesystem::path> shots,
                   std::vector<std::string> skipKinds)
    : route_(std::move(route)), shots_(std::move(shots)), skipKinds_(std::move(skipKinds)) {
  // Script errors, assets that fail to load and the like only reach the log: any of them fails the
  // run.
  core::addLogSink([errors = errors_](core::LogLevel level, std::string_view) {
    if (level == core::LogLevel::Error) {
      errors->fetch_add(1, std::memory_order_relaxed);
    }
  });
}

Autotest::~Autotest() {
  if (platform_ != nullptr && gamepad_) {
    platform_->detachVirtualGamepad(*gamepad_);
  }
}

AutotestDevice Autotest::deviceOf(const AutotestStep& step) const {
  return step.device.value_or(route_.device);
}

void Autotest::fail(std::string message) {
  if (!result_) {
    result_ = std::move(message);
  }
}

void Autotest::mapChanged(const MapChangeTiming& timing) {
  mapChanges_.push_back(timing);
}

void Autotest::frame(const AutotestHost& host, std::uint64_t ticks, double now,
                     std::vector<platform::Event>& events) {
  platform_ = host.platform;
  if (finished()) {
    return;
  }
  if (runStart_ < 0.0) {
    runStart_ = now;
    core::logInfo("autotest: route {} ({}), {} steps", route_.name, deviceName(route_.device),
                  route_.steps.size());
    if (route_.device == AutotestDevice::Gamepad) {
      static_cast<void>(ensureGamepad(host));
    }
  }
  if (now - runStart_ > route_.timeout) {
    fail(std::format("the route took more than {:.0f} s", route_.timeout));
    return;
  }
  runActions(ticks, events);
  if (actions_.empty() && stepIndex_ < route_.steps.size()) {
    const AutotestStep& step = route_.steps[stepIndex_];
    if (!progress_) {
      progress_ = std::make_unique<Progress>();
      progress_->start = now;
      progress_->mapSerial = host.scene != nullptr ? host.scene->mapSerial() : 0;
    }
    if (host.scene == nullptr && (step.kind == Kind::WalkTo || step.kind == Kind::Click)) {
      failStep(messages::NoGame);
    } else if (now - progress_->start > step.timeout && step.kind != Kind::Wait) {
      failStep(std::format("timed out after {:.0f} s; {}", step.timeout, where(host.scene)));
    } else if (runStep(host, step, now)) {
      finishStep(now);
    }
    runActions(ticks, events);
  }
  if (!finished() && stepIndex_ >= route_.steps.size() && actions_.empty()) {
    result_ = std::string{};
  }
}

void Autotest::runActions(std::uint64_t ticks, std::vector<platform::Event>& events) {
  while (!actions_.empty()) {
    Action& action = actions_.front();
    if (ticks < lastActionTick_ + static_cast<std::uint64_t>(action.afterTicks)) {
      return;
    }
    if (action.run) {
      action.run(events);
    }
    lastActionTick_ = ticks;
    actions_.pop_front();
  }
}

void Autotest::finishStep(double now) {
  const AutotestStep& step = route_.steps[stepIndex_];
  const double seconds = now - progress_->start;
  std::string line =
      std::format("[{}/{}] {}{}{}: done in {:.2f} s", stepIndex_ + 1, route_.steps.size(),
                  kindName(step.kind), step.text.empty() ? "" : " ", step.text, seconds);
  core::logInfo("autotest: {}", line);
  stepLog_.push_back(std::move(line));
  progress_.reset();
  ++stepIndex_;
}

void Autotest::failStep(std::string_view problem) {
  const AutotestStep& step = route_.steps[stepIndex_];
  fail(std::format("step {} of {} ({}{}{}) failed: {}", stepIndex_ + 1, route_.steps.size(),
                   kindName(step.kind), step.text.empty() ? "" : " ", step.text, problem));
}

bool Autotest::runStep(const AutotestHost& host, const AutotestStep& step, double now) {
  if (std::ranges::find(skipKinds_, kindName(step.kind)) != skipKinds_.end()) {
    std::string note =
        std::format("step {} ({}) skipped by --skip-steps", stepIndex_ + 1, kindName(step.kind));
    core::logWarning("autotest: {}", note);
    skipped_.push_back(std::move(note));
    return true;
  }
  switch (step.kind) {
  case Kind::Wait:
    return waitStep(host, step, now);
  case Kind::WalkTo:
    return walkStep(host, step, now);
  case Kind::Menu:
    return menuStep(host, step, now);
  case Kind::MenuSelect:
    return menuSelectStep(host, step);
  case Kind::ExpectSetting:
    return settingText(host, step.name) == step.text;
  case Kind::Screenshot:
    return screenshotStep(host, step);
  case Kind::Resize:
  case Kind::Fullscreen:
    return resizeStep(host, step, now);
  case Kind::Press:
  case Kind::Release:
  case Kind::Tap:
  case Kind::Axis:
    return controlStep(host, step);
  case Kind::Click:
    return clickStep(host, step);
  case Kind::Script:
    return scriptStep(host, step);
  case Kind::Remember:
    return rememberStep(host, step);
  case Kind::ExpectChange:
    return expectChangeStep(host, step);
  case Kind::ScriptIf:
    return scriptIfStep(host, step);
  }
  return true;
}

bool Autotest::rememberStep(const AutotestHost& host, const AutotestStep& step) {
  remembered_.insert_or_assign(step.name, settingText(host, step.name));
  return true;
}

bool Autotest::expectChangeStep(const AutotestHost& host, const AutotestStep& step) {
  const auto remembered = remembered_.find(step.name);
  if (remembered == remembered_.end()) {
    failStep(std::format("{} was not remembered", step.name));
    return false;
  }
  return movedAsAsked(remembered->second, settingText(host, step.name), step.text);
}

bool Autotest::scriptIfStep(const AutotestHost& host, const AutotestStep& step) {
  if (const std::string unmet = unmetCondition(host, step.until); !unmet.empty()) {
    core::logInfo("{}", core::formatMessage(messages::ScriptIfNotRun, stepIndex_ + 1, unmet));
    return true;
  }
  return scriptStep(host, step);
}

bool Autotest::waitStep(const AutotestHost& host, const AutotestStep& step, double now) {
  const double waited = now - progress_->start;
  if (waited < step.seconds) {
    return false;
  }
  const std::string unmet = unmetCondition(host, step.until);
  if (unmet.empty()) {
    return true;
  }
  if (waited > step.timeout) {
    failStep(std::format("{} after {:.0f} s; {}", unmet, step.timeout, where(host.scene)));
  }
  return false;
}

std::string Autotest::unmetCondition(const AutotestHost& host, const AutotestCondition& until) {
  const bool hasCondition = until.map.has_value() || until.partnerWithin.has_value() ||
                            until.playerNear.has_value() || until.menuOpen.has_value() ||
                            until.mapNameShown.has_value() || until.shown.has_value() ||
                            until.hidden.has_value() || until.partnerHas.has_value() ||
                            until.setting.has_value() || until.partnerModel.has_value();
  if (!hasCondition) {
    return {};
  }
  if (host.scene == nullptr) {
    if (until.mapNameShown || until.map || until.partnerWithin || until.playerNear ||
        until.partnerModel) {
      return std::string{messages::NoGame};
    }
    return unmetInUi(host, until);
  }
  const game::field::Field& field = host.scene->field();
  const glm::vec3 player = field.player().position;
  std::string unmet;
  if (until.mapNameShown) {
    if (field.mapNameShown() != std::optional<std::string_view>{*until.mapNameShown}) {
      unmet = core::formatMessage(messages::MapNameNotShown, *until.mapNameShown);
    }
  } else if (field.mapChangePending()) {
    unmet = "a map change is pending";
  } else if (until.map && field.mapId() != *until.map) {
    unmet = std::format("the map is {}, not {}", field.mapId(), *until.map);
  } else if (until.partnerWithin &&
             groundDistance(player, field.partner().position) > *until.partnerWithin) {
    unmet = std::format("the partner is farther than {:.0f}", *until.partnerWithin);
  } else if (until.playerNear && std::hypot(player.x - until.playerNear->x,
                                            player.z - until.playerNear->y) > until.playerRadius) {
    unmet = std::format("the player is not within {:.0f} of ({:.0f}, {:.0f})", until.playerRadius,
                        until.playerNear->x, until.playerNear->y);
  } else if (until.partnerModel && host.scene->partnerModel().str() != *until.partnerModel) {
    unmet = std::format("the partner is drawn as {}, not {}", host.scene->partnerModel().str(),
                        *until.partnerModel);
  }
  return unmet.empty() ? unmetInUi(host, until) : unmet;
}

std::string Autotest::unmetInUi(const AutotestHost& host, const AutotestCondition& until) {
  std::string unmet;
  if (until.menuOpen && host.ui->isVisible(MenuDocument) != *until.menuOpen) {
    unmet = *until.menuOpen ? "the menu is closed" : "the menu is open";
  } else if (until.shown && !host.ui->isVisible(*until.shown)) {
    unmet = std::format("{} is not shown", *until.shown);
  } else if (until.hidden && host.ui->isVisible(*until.hidden)) {
    unmet = std::format("{} is still shown", *until.hidden);
  } else if (until.partnerHas && !partnerHas(host, *until.partnerHas)) {
    unmet = std::format("the partner is not {}", *until.partnerHas);
  } else if (until.setting && settingText(host, until.setting->first) != until.setting->second) {
    unmet = std::format("{} is {}, not {}", until.setting->first,
                        settingText(host, until.setting->first), until.setting->second);
  }
  return unmet;
}

bool Autotest::walkStep(const AutotestHost& host, const AutotestStep& step, double now) {
  Progress& progress = *progress_;
  const AutotestDevice device = deviceOf(step);
  if (device == AutotestDevice::Gamepad && !ensureGamepad(host)) {
    return false;
  }
  const game::field::Field& field = host.scene->field();
  const glm::vec3 position = field.player().position;
  const auto plan = [&] {
    const assets::MapCollision& collision = field.collision();
    const std::vector<assets::CollisionCell> goals = goalCells(collision, step);
    progress.path = game::field::findPath(
        collision, position, goals, field.rules().player.radius, [&](assets::CollisionCell cell) {
          const std::int32_t value = collision.trigger(cell).value_or(0);
          return !field.keepsOff(value) || value == step.trigger;
        });
    // A walk to a point ends on it, not on the centre of its cell.
    if (step.point && !progress.path.empty()) {
      progress.path.emplace_back(step.point->x, progress.path.back().y, step.point->y);
    }
    progress.clickBackoff = 0;
    // Planning is the driver's work, not the game's: its frame stays out of the steady frame times.
    host.stats->skipNext(1);
  };
  if (!progress.begun) {
    progress.begun = true;
    progress.lastPosition = position;
    progress.lastProgress = now;
    plan();
    if (progress.path.empty()) {
      failStep(std::format("no path from ({:.0f}, {:.0f}) in {}", position.x, position.z,
                           field.mapId()));
      return false;
    }
  }
  // Arrived: through the warp, or at the point.
  bool arrived = host.scene->mapSerial() != progress.mapSerial;
  if (!step.trigger && step.point) {
    // A walk to a point stays on its map: the same point on the map behind an exit is not it.
    if (arrived) {
      failStep(std::format("left the map on the way to the point; {}", where(host.scene)));
      return false;
    }
    arrived = std::hypot(position.x - step.point->x, position.z - step.point->y) <= step.radius;
  }
  if (arrived) {
    releaseMovement();
    return true;
  }
  if (field.mapChangePending()) {
    releaseMovement();
    return step.stopOnWarp;
  }
  // Free exploration asks where to go: the route answers in the chooser (wait for it shown, then
  // menu_select).
  if (field.destinationChoice()) {
    releaseMovement();
    return true;
  }
  bool stalled = false;
  if (groundDistance(position, progress.lastPosition) > ProgressDistance) {
    progress.lastPosition = position;
    progress.lastProgress = now;
  } else if (now - progress.lastProgress > StallSeconds) {
    core::logInfo("autotest: stuck at ({:.0f}, {:.0f}); planning again", position.x, position.z);
    progress.lastProgress = now;
    progress.lastClick = -1.0;
    ++progress.attempts;
    stalled = true;
    plan();
  }
  if (progress.path.empty()) {
    failStep(std::format("no path any more; {}", where(host.scene)));
    return false;
  }
  // The next waypoint is done when reached, or when the one after it is nearer than it is to that
  // one (passed).
  const float reach = device == AutotestDevice::Keyboard ? KeyWaypointReach : WaypointReach;
  while (progress.path.size() > 1 && (groundDistance(position, progress.path[0]) <= reach ||
                                      groundDistance(position, progress.path[1]) <
                                          groundDistance(progress.path[0], progress.path[1]))) {
    progress.path.erase(progress.path.begin());
  }
  if (device == AutotestDevice::Mouse) {
    walkWithMouse(host, now);
    return false;
  }
  const glm::vec3 aim = progress.path.front() - position;
  const float length = std::hypot(aim.x, aim.z);
  glm::vec2 stick{0.0F};
  if (length > 1e-3F) {
    // Up on the stick walks along the camera's ground forward, right along its right
    // (game::field::FieldInput).
    const glm::vec3 forward = field.cameraForward();
    const glm::vec2 direction{aim.x / length, aim.z / length};
    const glm::vec2 right{-forward.z, forward.x};
    stick = {glm::dot(direction, right), glm::dot(direction, glm::vec2{forward.x, forward.z})};
  }
  // Stuck on a wall (eight key directions cannot follow every line): push for a moment at an angle
  // to the aim, to one side and then the other, so the collision slides the player along the wall.
  if (stalled) {
    constexpr std::array<float, 4> Turns{0.785F, -0.785F, 1.571F, -1.571F};
    const std::size_t attempt = static_cast<std::size_t>(progress.attempts) % Turns.size();
    assert(attempt < Turns.size());
    const float turn = Turns[attempt];
    progress.unstick = glm::vec2{(stick.x * std::cos(turn)) - (stick.y * std::sin(turn)),
                                 (stick.x * std::sin(turn)) + (stick.y * std::cos(turn))};
    progress.unstickUntil = now + UnstickSeconds;
  }
  if (now < progress.unstickUntil) {
    stick = progress.unstick;
  }
  steer(device, stick, step.walk);
  return false;
}

void Autotest::walkWithMouse(const AutotestHost& host, double now) {
  Progress& progress = *progress_;
  const game::field::Field& field = host.scene->field();
  // A new click when the last one was reached (or did not take), and now and then to aim farther
  // along the path.
  constexpr double RetryClickSeconds = 0.4;
  constexpr double RefreshClickSeconds = 3.0;
  const double sinceClick = progress.lastClick < 0.0 ? 1e9 : now - progress.lastClick;
  progress.clickTook = progress.clickTook || field.hasMoveTarget();
  if (field.hasMoveTarget() ? sinceClick < RefreshClickSeconds : sinceClick < RetryClickSeconds) {
    return;
  }
  // A click that set no target (a point the field cannot reach) is followed by one nearer along the
  // path.
  progress.clickBackoff =
      progress.lastClick >= 0.0 && !progress.clickTook ? progress.clickBackoff + 1 : 0;
  const rhi::Extent extent = host.renderer->targetExtent();
  const glm::vec2 size{static_cast<float>(extent.width), static_cast<float>(extent.height)};
  const float margin = ClickMargin * std::min(size.x, size.y);
  const auto menuButton = host.ui->elementCenter("hud", "menu-button");
  // The farthest point along the path that is on screen, away from the edges and the HUD's menu
  // button. The path is sampled between its waypoints, so a walk whose next waypoint is off screen
  // still clicks towards it.
  constexpr float SampleSpacing = 25.0F;
  constexpr int SamplesPerBackoff = 4;
  std::vector<glm::vec3> samples;
  glm::vec3 from = field.player().position;
  for (const glm::vec3& waypoint : progress.path) {
    const float length = groundDistance(from, waypoint);
    const int count = std::max(1, static_cast<int>(length / SampleSpacing));
    for (int index = 1; index <= count; ++index) {
      samples.push_back(from + (waypoint - from) *
                                   (static_cast<float>(index) / static_cast<float>(count)));
    }
    from = waypoint;
  }
  std::optional<glm::vec2> chosen;
  glm::vec3 chosenWorld{0.0F};
  int skipped = 0;
  for (const glm::vec3& point : std::views::reverse(samples)) {
    const auto window = host.scene->worldToWindow(point);
    if (!window || window->x < margin || window->y < margin || window->x > size.x - margin ||
        window->y > size.y - margin) {
      continue;
    }
    if (menuButton &&
        glm::distance(*window, glm::vec2{menuButton->x, menuButton->y}) < 0.2F * size.y) {
      continue;
    }
    if (skipped < progress.clickBackoff * SamplesPerBackoff) {
      ++skipped;
      continue;
    }
    chosen = *window;
    chosenWorld = point;
    break;
  }
  const glm::vec3 position = field.player().position;
  if (!chosen && !samples.empty()) {
    // Nothing ahead is on screen (the player is at the edge, walking off it): point at the edge of
    // the window, in the direction the path leaves the player, and hold.
    const glm::vec3 ahead = samples[std::min<std::size_t>(samples.size() - 1, 3)];
    const auto here = host.scene->worldToWindow(position);
    const auto there = host.scene->worldToWindow(ahead);
    if (here && there && glm::distance(*here, *there) > 1.0F) {
      const glm::vec2 direction = glm::normalize(*there - *here);
      float reachEdge = std::numeric_limits<float>::max();
      for (int axis = 0; axis < 2; ++axis) {
        const float low = margin;
        const float high = (axis == 0 ? size.x : size.y) - margin;
        if (direction[axis] > 1e-3F) {
          reachEdge = std::min(reachEdge, (high - (*here)[axis]) / direction[axis]);
        } else if (direction[axis] < -1e-3F) {
          reachEdge = std::min(reachEdge, (low - (*here)[axis]) / direction[axis]);
        }
      }
      chosen = *here + direction * std::max(reachEdge, 0.0F);
      chosenWorld = position;
    }
  }
  if (!chosen) {
    progress.clickBackoff = 0;
    return;
  }
  // The farthest point on screen is next to the player but the path goes on (off screen, as an exit
  // at the edge of the screen): hold the button there instead, which walks towards the pointer
  // until released.
  constexpr float HoldDistance = 80.0F;
  constexpr int HoldTicks = 45;
  if (groundDistance(position, chosenWorld) < HoldDistance &&
      groundDistance(position, samples.back()) > HoldDistance) {
    core::logInfo(
        "autotest: hold at ({:.0f}, {:.0f}) towards ({:.0f}, {:.0f}); player at ({:.0f}, {:.0f})",
        chosen->x, chosen->y, chosenWorld.x, chosenWorld.z, position.x, position.z);
    // The hold does not count as progress: a player who does not move under it is still stuck.
    holdAt(*chosen, HoldTicks);
    progress.lastClick = -1.0;
    return;
  }
  core::logInfo(
      "autotest: click at ({:.0f}, {:.0f}) for ({:.0f}, {:.0f}); player at ({:.0f}, {:.0f})",
      chosen->x, chosen->y, chosenWorld.x, chosenWorld.z, position.x, position.z);
  clickAt(*chosen);
  progress.lastClick = now;
  progress.clickTook = false;
}

bool Autotest::menuStep(const AutotestHost& host, const AutotestStep& step, double now) {
  Progress& progress = *progress_;
  const AutotestDevice device = deviceOf(step);
  if (host.ui->isVisible(MenuDocument) == step.flag) {
    return true;
  }
  if (device == AutotestDevice::Gamepad && !ensureGamepad(host)) {
    return false;
  }
  constexpr double RetrySeconds = 1.0;
  constexpr int MaxTries = 6;
  // A tap toggles the menu, so the next one waits until the last had time to show.
  if (progress.lastTry >= 0.0 && now - progress.lastTry < RetrySeconds) {
    return false;
  }
  if (progress.attempts >= MaxTries) {
    failStep(
        std::format("the menu did not {} after {} tries", step.flag ? "open" : "close", MaxTries));
    return false;
  }
  ++progress.attempts;
  progress.lastTry = now;
  if (device != AutotestDevice::Mouse) {
    tapAction(device, step.flag ? "menu" : "cancel");
    return false;
  }
  if (step.flag) {
    const auto button = host.ui->elementCenter("hud", "menu-button");
    if (!button) {
      failStep("the HUD has no menu-button");
      return false;
    }
    clickAt({button->x, button->y});
  } else {
    // A right click backs out of the menu, as cancel does.
    const rhi::Extent extent = host.renderer->targetExtent();
    clickAt({static_cast<float>(extent.width) / 2.0F, static_cast<float>(extent.height) / 2.0F},
            platform::MouseButton::Right);
  }
  return false;
}

bool Autotest::menuSelectStep(const AutotestHost& host, const AutotestStep& step) {
  Progress& progress = *progress_;
  if (progress.phase == 1) {
    return true;
  }
  const std::string_view menu = shownMenu(*host.ui);
  if (!host.ui->isVisible(menu)) {
    failStep("the menu is not open");
    return false;
  }
  const AutotestDevice device = deviceOf(step);
  if (device == AutotestDevice::Mouse) {
    const auto center = host.ui->elementCenter(menu, step.text);
    if (!center) {
      failStep("the menu has no such entry");
      return false;
    }
    if (center->x <= 0.0F && center->y <= 0.0F) {
      return false; // not laid out yet
    }
    clickAt({center->x, center->y});
    progress.phase = 1;
    return false;
  }
  if (device == AutotestDevice::Gamepad && !ensureGamepad(host)) {
    return false;
  }
  if (host.ui->focusedElementId(menu) == step.text) {
    tapAction(device, "confirm");
    progress.phase = 1;
    return false;
  }
  // Down through the page, then up if it does not wrap round, then right and left through a row
  // (the battle's commands).
  constexpr int DownTries = 8;
  constexpr int UpTries = 16;
  constexpr int RightTries = 28;
  constexpr int MaxTries = 40;
  if (progress.attempts >= MaxTries) {
    failStep(std::format("cannot focus it; the focus is on '{}'", host.ui->focusedElementId(menu)));
    return false;
  }
  std::string_view direction = "left";
  if (progress.attempts < DownTries) {
    direction = "down";
  } else if (progress.attempts < UpTries) {
    direction = "up";
  } else if (progress.attempts < RightTries) {
    direction = "right";
  }
  tapAction(device, direction);
  ++progress.attempts;
  return false;
}

bool Autotest::scriptStep(const AutotestHost& host, const AutotestStep& step) {
  if (host.scripts == nullptr) {
    failStep("the game has no scripts");
    return false;
  }
  if (auto ran = host.scripts->runString(step.text, "autotest"); !ran) {
    failStep(ran.error().message);
    return false;
  }
  return true;
}

bool Autotest::screenshotStep(const AutotestHost& host, const AutotestStep& step) {
  Progress& progress = *progress_;
  if (!shots_) {
    core::logInfo("autotest: screenshot {} skipped (no --shots folder)", step.text);
    return true;
  }
  if (!progress.shot) {
    progress.shot = std::make_shared<int>(0);
    progress.shotMessage = std::make_shared<std::string>();
    ++screenshots_;
    const std::filesystem::path folder = *shots_;
    const std::string stem = std::format("{}-{:02}-{}", route_.name, screenshots_, step.text);
    host.renderer->captureNextFrame([state = progress.shot, message = progress.shotMessage, folder,
                                     stem](core::Result<rhi::CapturedImage> captured) {
      if (!captured) {
        *state = 2;
        *message = captured.error().message;
        return;
      }
      const assets::Image image{.width = captured->width,
                                .height = captured->height,
                                .pixels = std::move(captured->pixels)};
      const auto png = assets::encodePng(image);
      const std::filesystem::path path =
          folder / std::format("{}-{}x{}.png", stem, image.width, image.height);
      std::ofstream file{path, std::ios::binary};
      if (!png || !file.write(reinterpret_cast<const char*>(png->data()),
                              static_cast<std::streamsize>(png->size()))) {
        *state = 2;
        *message = png ? std::format("cannot write {}", path.string()) : png.error().message;
        return;
      }
      *state = 1;
      *message = path.string();
    });
    host.stats->skipNext(SettleFrames);
    return false;
  }
  if (*progress.shot == 0) {
    return false;
  }
  if (*progress.shot == 2) {
    failStep(*progress.shotMessage);
    return false;
  }
  core::logInfo("autotest: screenshot {}", *progress.shotMessage);
  return true;
}

bool Autotest::resizeStep(const AutotestHost& host, const AutotestStep& step, double now) {
  Progress& progress = *progress_;
  host.stats->skipNext(SettleFrames);
  const rhi::Extent extent = host.renderer->targetExtent();
  if (!progress.begun) {
    progress.begun = true;
    core::Result<void> changed;
    if (step.kind == Kind::Fullscreen) {
      changed = host.window->setFullscreen(step.flag);
    } else {
      if (host.window->isFullscreen()) {
        changed = host.window->setFullscreen(false);
      }
      if (changed) {
        changed = host.window->setSize(step.width, step.height);
      }
    }
    if (!changed) {
      failStep(changed.error().message);
    }
    progress.lastExtent = extent;
    return false;
  }
  if (extent.width != progress.lastExtent.width || extent.height != progress.lastExtent.height) {
    progress.lastExtent = extent;
    progress.stableFrames = 0;
    return false;
  }
  ++progress.stableFrames;
  // Some window managers apply a change late: give them a moment before judging the size.
  constexpr double MinimumSeconds = 0.5;
  if (progress.stableFrames < StableFrames || now - progress.start < MinimumSeconds) {
    return false;
  }
  if (step.kind == Kind::Fullscreen) {
    if (host.window->isFullscreen() != step.flag) {
      return false;
    }
    core::logInfo("autotest: fullscreen {} at {}x{}", step.flag ? "on" : "off", extent.width,
                  extent.height);
    return true;
  }
  // setSize takes window units, which are not pixels on high-density displays.
  const platform::PixelSize windowSize = host.window->windowSize();
  if (windowSize.width == step.width && windowSize.height == step.height) {
    return true;
  }
  if (now - progress.start <= step.timeout / 2.0) {
    return false;
  }
  // A window larger than the desktop's usable area is up to the window manager, which may refuse
  // it: the step is skipped there, and the route runs it on a larger screen
  // (docs/guides/prueba-vertical.md).
  const auto usable = host.window->usableDisplaySize();
  if (usable && (step.width > usable->width || step.height > usable->height)) {
    std::string note = std::format(
        "resize to {}x{} skipped: the desktop's usable area is {}x{}; the window is {}x{}",
        step.width, step.height, usable->width, usable->height, windowSize.width,
        windowSize.height);
    core::logWarning("autotest: {}", note);
    skipped_.push_back(std::move(note));
    return true;
  }
  failStep(std::format("the window is {}x{} ({}x{} pixels), not {}x{}", windowSize.width,
                       windowSize.height, extent.width, extent.height, step.width, step.height));
  return false;
}

bool Autotest::controlStep(const AutotestHost& host, const AutotestStep& step) {
  if (progress_->phase == 1) {
    return true;
  }
  progress_->phase = 1;
  if (step.kind == Kind::Axis) {
    if (!ensureGamepad(host)) {
      progress_->phase = 0;
      return false;
    }
    const auto named = gamepadAxisNamed(step.text);
    axis(named.value_or(platform::GamepadAxis::LeftX), step.value);
    return false;
  }
  const bool down = step.kind != Kind::Release;
  if (step.name == "key") {
    const platform::KeyCode code = keyNamed(step.text).value_or(platform::KeyCode{0});
    if (step.kind == Kind::Tap) {
      key(code, true);
      key(code, false, TapTicks);
    } else {
      key(code, down);
    }
    return false;
  }
  if (!ensureGamepad(host)) {
    progress_->phase = 0;
    return false;
  }
  const platform::GamepadButton pressed =
      gamepadButtonNamed(step.text).value_or(platform::GamepadButton::South);
  if (step.kind == Kind::Tap) {
    button(pressed, true);
    button(pressed, false, TapTicks);
  } else {
    button(pressed, down);
  }
  return false;
}

bool Autotest::clickStep(const AutotestHost& host, const AutotestStep& step) {
  if (progress_->phase == 1) {
    return true;
  }
  const glm::vec2 point = step.point.value_or(glm::vec2{0.0F});
  const auto window = host.scene->worldToWindow({point.x, 0.0F, point.y});
  if (!window) {
    failStep("the point is not in view");
    return false;
  }
  clickAt(*window);
  progress_->phase = 1;
  return false;
}

void Autotest::key(platform::KeyCode code, bool down, int afterTicks) {
  if (down) {
    keysDown_.push_back(code);
  } else {
    std::erase(keysDown_, code);
  }
  actions_.push_back(
      {.afterTicks = afterTicks, .run = [code, down](std::vector<platform::Event>& events) {
         events.emplace_back(platform::KeyChanged{.key = code, .down = down, .repeat = false});
       }});
}

void Autotest::button(platform::GamepadButton pressed, bool down, int afterTicks) {
  actions_.push_back(
      {.afterTicks = afterTicks, .run = [this, pressed, down](std::vector<platform::Event>&) {
         if (platform_ != nullptr && gamepad_) {
           platform_->setVirtualGamepadButton(*gamepad_, pressed, down);
         }
       }});
}

void Autotest::axis(platform::GamepadAxis moved, float value) {
  actions_.push_back({.afterTicks = 0, .run = [this, moved, value](std::vector<platform::Event>&) {
                        if (platform_ != nullptr && gamepad_) {
                          platform_->setVirtualGamepadAxis(*gamepad_, moved, value);
                        }
                      }});
}

void Autotest::tapAction(AutotestDevice device, std::string_view action) {
  namespace keys = input::keys;
  using platform::GamepadButton;
  struct Control {
    std::string_view action;
    platform::KeyCode key;
    GamepadButton button;
  };
  constexpr std::array<Control, 7> Controls{{
      {.action = "menu", .key = keys::Tab, .button = GamepadButton::Start},
      {.action = "confirm", .key = keys::Return, .button = GamepadButton::South},
      {.action = "cancel", .key = keys::Escape, .button = GamepadButton::East},
      {.action = "up", .key = keys::Up, .button = GamepadButton::DpadUp},
      {.action = "down", .key = keys::Down, .button = GamepadButton::DpadDown},
      {.action = "left", .key = keys::Left, .button = GamepadButton::DpadLeft},
      {.action = "right", .key = keys::Right, .button = GamepadButton::DpadRight},
  }};
  const auto control = std::ranges::find(Controls, action, &Control::action);
  if (control == Controls.end()) {
    return;
  }
  if (device == AutotestDevice::Gamepad) {
    button(control->button, true);
    button(control->button, false, TapTicks);
  } else {
    key(control->key, true);
    key(control->key, false, TapTicks);
  }
  actions_.push_back({.afterTicks = TapTicks, .run = nullptr});
}

void Autotest::clickAt(glm::vec2 window, platform::MouseButton pressed) {
  actions_.push_back(
      {.afterTicks = 0, .run = [window, pressed](std::vector<platform::Event>& events) {
         events.emplace_back(platform::MouseMoved{.x = window.x, .y = window.y});
         events.emplace_back(platform::MouseButtonChanged{.button = pressed, .down = true});
         events.emplace_back(platform::MouseButtonChanged{.button = pressed, .down = false});
       }});
  actions_.push_back({.afterTicks = TapTicks, .run = nullptr});
}

void Autotest::holdAt(glm::vec2 window, int ticks) {
  actions_.push_back({.afterTicks = 0, .run = [window](std::vector<platform::Event>& events) {
                        events.emplace_back(platform::MouseMoved{.x = window.x, .y = window.y});
                        events.emplace_back(platform::MouseButtonChanged{
                            .button = platform::MouseButton::Left, .down = true});
                      }});
  actions_.push_back({.afterTicks = ticks, .run = [](std::vector<platform::Event>& events) {
                        events.emplace_back(platform::MouseButtonChanged{
                            .button = platform::MouseButton::Left, .down = false});
                      }});
}

void Autotest::steer(AutotestDevice device, glm::vec2 stick, bool walk) {
  namespace keys = input::keys;
  if (device == AutotestDevice::Gamepad) {
    constexpr float Change = 0.02F;
    if (glm::distance(stick, stick_) > Change) {
      stick_ = stick;
      axis(platform::GamepadAxis::LeftX, stick.x);
      // Down is positive on a gamepad's stick.
      axis(platform::GamepadAxis::LeftY, -stick.y);
    }
    if (walk != walkButtonDown_) {
      walkButtonDown_ = walk;
      button(platform::GamepadButton::West, walk);
    }
    return;
  }
  std::vector<platform::KeyCode> wanted;
  if (stick.y > KeyThreshold) {
    wanted.push_back(keys::W);
  } else if (stick.y < -KeyThreshold) {
    wanted.push_back(keys::S);
  }
  if (stick.x > KeyThreshold) {
    wanted.push_back(keys::D);
  } else if (stick.x < -KeyThreshold) {
    wanted.push_back(keys::A);
  }
  if (walk) {
    wanted.push_back(keys::LeftShift);
  }
  for (const platform::KeyCode held : std::vector<platform::KeyCode>{keysDown_}) {
    if (std::ranges::find(wanted, held) == wanted.end()) {
      key(held, false);
    }
  }
  for (const platform::KeyCode code : wanted) {
    if (std::ranges::find(keysDown_, code) == keysDown_.end()) {
      key(code, true);
    }
  }
}

void Autotest::releaseMovement() {
  for (const platform::KeyCode held : std::vector<platform::KeyCode>{keysDown_}) {
    key(held, false);
  }
  // Whichever device the route uses: a gamepad step may have held it.
  if (walkButtonDown_) {
    walkButtonDown_ = false;
    button(platform::GamepadButton::West, false);
  }
  if (gamepad_ && (stick_.x != 0.0F || stick_.y != 0.0F)) {
    stick_ = glm::vec2{0.0F};
    axis(platform::GamepadAxis::LeftX, 0.0F);
    axis(platform::GamepadAxis::LeftY, 0.0F);
  }
}

bool Autotest::ensureGamepad(const AutotestHost& host) {
  if (!gamepad_) {
    auto attached = host.platform->attachVirtualGamepad(route_.gamepadFamily);
    if (!attached) {
      fail(std::format("no virtual gamepad: {}", attached.error().message));
      return false;
    }
    gamepad_ = *attached;
    core::logInfo("autotest: virtual gamepad {} attached", *gamepad_);
  }
  return std::ranges::any_of(host.input->gamepads(), [this](const input::ConnectedGamepad& pad) {
    return pad.id == *gamepad_;
  });
}

void Autotest::summarize(const AutotestHost& host) const {
  core::logInfo("autotest: route {} ({}), {} of {} steps", route_.name, deviceName(route_.device),
                stepLog_.size(), route_.steps.size());
  for (const MapChangeTiming& change : mapChanges_) {
    core::logInfo("autotest: map change {} -> {}: loaded in {:.2f} ms, ready in {:.2f} ms",
                  change.from, change.to, change.loadMilliseconds, change.totalMilliseconds);
  }
  for (const std::string& note : skipped_) {
    core::logWarning("autotest: {}", note);
  }
  core::logInfo("autotest: frame times, all: {}", FrameStats::describe(host.stats->overall()));
  core::logInfo("autotest: frame times, steady: {}", FrameStats::describe(host.stats->steady()));
}

int Autotest::verdict(bool validationActive) const {
  std::string failure = result_.value_or("the run stopped before its end");
  if (validationActive) {
    const std::uint64_t messages = rhi::Device::validationMessageCount();
    core::logInfo("autotest: validation layer on, {} messages", messages);
    if (messages > 0 && failure.empty()) {
      failure = std::format("the validation layer reported {} messages", messages);
    }
  } else {
    // Without the layer there is nothing to count, so the run passes or fails on its route and its
    // log alone.
    core::logInfo("autotest: validation layer {}, messages not checked", validationState(false));
  }
  const std::uint64_t errors = errors_->load(std::memory_order_relaxed);
  core::logInfo("autotest: {} [error] lines in the log", errors);
  if (errors > 0 && failure.empty()) {
    failure = std::format("the log has {} [error] lines", errors);
  }
  if (!failure.empty()) {
    core::logError("autotest: FAIL: {}", failure);
    return EXIT_FAILURE;
  }
  // Skipping the steps that move the player or check the field leaves the run unproven: it is only
  // partial.
  std::vector<std::string_view> unchecked;
  for (const std::string_view kind : {std::string_view{"walk_to"}, std::string_view{"wait"}}) {
    if (std::ranges::find(skipKinds_, kind) != skipKinds_.end()) {
      unchecked.push_back(kind);
    }
  }
  if (!unchecked.empty()) {
    std::string kinds;
    for (const std::string_view kind : unchecked) {
      kinds += kinds.empty() ? std::string{kind} : std::format(", {}", kind);
    }
    core::logWarning("autotest: PASS (partial: skipped {})", kinds);
    return PartialExitCode;
  }
  core::logInfo("autotest: PASS");
  return EXIT_SUCCESS;
}

} // namespace dwe::app
