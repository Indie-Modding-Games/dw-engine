#include "autotest.hpp"

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

#include "battle_screen.hpp"
#include "common.hpp"
#include "device_names.hpp"
#include "field_items_screen.hpp"
#include "messages.hpp"
#include "options.hpp"
#include "screenshot.hpp"
#include "story_text_rows.hpp"
#include "tournament_bracket_screen.hpp"

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
// How often a walk_to answers what stops it on the way: the pages of the story's textbox in an
// encounter (confirm), a chooser of another cell (cancel).
constexpr double AnswerSeconds = 1.0;
// Keys turn a direction into one of eight: a component counts past sin(22.5 degrees).
constexpr float KeyThreshold = 0.383F;
// How long the driver holds a tapped control, and waits after it, in logic ticks.
constexpr int TapTicks = 3;
// How long a walk waits between two confirms of a pickup notice it ran into.
constexpr double NoticeConfirmSeconds = 0.5;
// A click-to-move point must be this far inside the window (a fraction of its smaller side).
constexpr float ClickMargin = 0.02F;
// Frames after a resize, a screenshot or a mode change that the steady frame times leave out.
constexpr int SettleFrames = 10;
// The exit code of a run that passed with walk_to or wait steps skipped by --skip-steps.
constexpr int PartialExitCode = 2;
// Frames the window size must keep before a resize counts as done.
constexpr int StableFrames = 5;

// The other documents menu_select picks from while they are open: the battle screen, the partner's
// menu, free exploration's chooser, the title screen's slot list, the save dialog, the sleep
// menu, the shop and the item keeper.
constexpr std::array<std::string_view, 9> ScreenDocuments{
    "partner_menu", "battle",    "destination_choice", "dialog",        "save_slots",
    "sleep_menu",   "shop_menu", "item_keeper",        "new_game_rules"};

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
constexpr std::array<std::string_view, 22> StepNames{
    "wait",          "walk_to",   "menu",       "menu_select", "expect_setting",
    "screenshot",    "resize",    "fullscreen", "press",       "release",
    "tap",           "axis",      "click",      "script",      "remember",
    "expect_change", "script_if", "hold",       "when",        "expect_text_fits",
    "expect_clear",  "hover"};

// What an expect_text_fits step names to check every shown document.
constexpr std::string_view AllDocuments = "all";

// What an expect_change step asks of a setting against its remembered value.
constexpr std::string_view ChangedUp = "up";
constexpr std::string_view ChangedDown = "down";
constexpr std::string_view ChangedAtAll = "changed";
// The key of a block of steps repeated in place (repeat: {times, steps}).
constexpr std::string_view RepeatKey = "repeat";
// The key of a block of steps run only if its conditions hold (when: {conditions, steps}).
constexpr std::string_view WhenKey = "when";
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
// The field's own value a route can read as a setting: how many times two characters stood in
// each other (Field::overlapEpisodes).
constexpr std::string_view FieldOverlapsSetting = "field.overlaps";

// The story's state a route can read as settings: story.trigger.<n> is true or false and
// story.var.<n> the variable's value, as the original's script state keeps them.
constexpr std::string_view StoryTriggerSetting = "story.trigger.";
constexpr std::string_view StoryVariableSetting = "story.var.";

std::optional<std::string> storySettingText(const game::story::StoryState& story,
                                            std::string_view name) {
  const bool trigger = name.starts_with(StoryTriggerSetting);
  if (!trigger && !name.starts_with(StoryVariableSetting)) {
    return std::nullopt;
  }
  const std::string_view digits =
      name.substr(trigger ? StoryTriggerSetting.size() : StoryVariableSetting.size());
  std::int32_t number = 0;
  const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
  if (error != std::errc{} || end != digits.data() + digits.size()) {
    return std::nullopt;
  }
  if (trigger) {
    const core::Result<bool> set = story.trigger(number);
    return set ? std::optional<std::string>{*set ? "true" : "false"} : std::nullopt;
  }
  const core::Result<std::uint8_t> value = story.variable(number);
  return value ? std::optional<std::string>{std::to_string(*value)} : std::nullopt;
}

// Whether an NPC is on the map now, by its script id: field.npc.<id> is true or false (the story
// puts some only now and then, such as the swamp's Otamamon).
constexpr std::string_view FieldNpcSetting = "field.npc.";

std::optional<std::string> fieldNpcText(const FieldScene& scene, std::string_view name) {
  if (!name.starts_with(FieldNpcSetting)) {
    return std::nullopt;
  }
  const std::string_view digits = name.substr(FieldNpcSetting.size());
  std::int32_t scriptId = 0;
  const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), scriptId);
  if (error != std::errc{} || end != digits.data() + digits.size()) {
    return std::nullopt;
  }
  return scene.field().npcs().positionOf(scriptId) ? "true" : "false";
}

// How many files the saves folder holds (--saves), its subfolders included: a game played without
// saving must leave it empty.
constexpr std::string_view SavesFilesSetting = "saves.files";

std::size_t savesFiles(const std::optional<std::filesystem::path>& folder) {
  std::error_code error;
  if (!folder || !std::filesystem::is_directory(*folder, error)) {
    return 0;
  }
  std::size_t files = 0;
  for (std::filesystem::recursive_directory_iterator it{*folder, error}, end; !error && it != end;
       it.increment(error)) {
    if (it->is_regular_file(error)) {
      ++files;
    }
  }
  return files;
}

// Whether an element of a document has a class: has_class.<document>#<element>.<class> is true or
// false, as which tab a page shows (has_class.partner_menu#tab-tech.selected).
constexpr std::string_view HasClassSetting = "has_class.";

std::optional<std::string> hasClassText(const ui::Ui& ui, std::string_view name) {
  if (!name.starts_with(HasClassSetting)) {
    return std::nullopt;
  }
  const std::string_view rest = name.substr(HasClassSetting.size());
  const std::size_t hash = rest.find('#');
  const std::size_t dot = rest.rfind('.');
  if (hash == std::string_view::npos || dot == std::string_view::npos || dot < hash) {
    return std::nullopt;
  }
  return ui.elementHasClass(rest.substr(0, hash), rest.substr(hash + 1, dot - hash - 1),
                            rest.substr(dot + 1))
             ? "true"
             : "false";
}

std::string settingText(const AutotestHost& host, std::string_view name) {
  if (std::optional<std::string> hasClass = hasClassText(*host.ui, name)) {
    return *std::move(hasClass);
  }
  if (name == SavesFilesSetting) {
    return std::to_string(savesFiles(host.saves));
  }
  if (name == FieldOverlapsSetting && host.scene != nullptr) {
    return std::to_string(host.scene->field().overlapEpisodes());
  }
  if (host.scene != nullptr) {
    if (std::optional<std::string> npc = fieldNpcText(*host.scene, name)) {
      return *std::move(npc);
    }
  }
  if (host.story != nullptr) {
    if (std::optional<std::string> story = storySettingText(*host.story, name)) {
      return *std::move(story);
    }
  }
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
    if (const core::YamlNode* when = step.isMapping() ? step.find(WhenKey) : nullptr) {
      AutotestStep guard;
      guard.kind = Kind::When;
      reader.expectKeys(*when, WhenKey, {"steps"},
                        {"map", "partner_within", "player_near", "radius", "menu_open", "shown",
                         "hidden", "partner_has", "setting"});
      readCondition(reader, *when, guard);
      std::vector<AutotestStep> block;
      readSteps(reader, reader.field(*when, "steps"), block);
      guard.block = block.size();
      steps.push_back(std::move(guard));
      steps.insert(steps.end(), block.begin(), block.end());
      continue;
    }
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
                      {"seconds", "ticks", "map", "partner_within", "player_near", "radius",
                       "menu_open", "map_name", "shown", "hidden", "partner_has", "setting",
                       "partner_model", "confirm", "answer", "win_after", "timeout"});
    if (const core::YamlNode* ticks = value.find("ticks")) {
      step.ticks = reader.integer<std::uint64_t>(*ticks);
    }
    if (const core::YamlNode* confirm = value.find("confirm")) {
      step.confirm = reader.boolean(*confirm);
    }
    if (const core::YamlNode* answer = value.find("answer")) {
      step.answer = reader.boolean(*answer);
    }
    if (const core::YamlNode* win = value.find("win_after")) {
      step.winAfter = reader.number(*win);
    }
    readCondition(reader, value, step);
    readCommon(reader, value, step);
  } else if (kind == "walk_to") {
    step.kind = Kind::WalkTo;
    step.timeout = 90.0;
    reader.expectKeys(value, "walk_to", {},
                      {"trigger", "point", "radius", "walk", "stop_on_warp", "stop_on_scene",
                       "stop_on_closed", "encounters", "device", "timeout"});
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
    if (const core::YamlNode* stop = value.find("stop_on_scene")) {
      step.stopOnScene = reader.boolean(*stop);
    }
    if (const core::YamlNode* stop = value.find("stop_on_closed")) {
      step.stopOnClosed = reader.boolean(*stop);
    }
    if (const core::YamlNode* encounters = value.find("encounters")) {
      step.winEncounters = encounters->isScalar() && encounters->text() == "win";
      step.encounters = step.winEncounters || reader.boolean(*encounters);
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
  } else if (kind == "expect_text_fits") {
    step.kind = Kind::ExpectTextFits;
    step.text = reader.text(value);
    // A box that is still opening fits once it is in place.
    step.timeout = 1.0;
  } else if (kind == "expect_clear") {
    step.kind = Kind::ExpectClear;
    step.text = reader.text(value);
    // A window that is still opening or moving is clear once it is in place.
    step.timeout = 1.0;
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
  } else if (kind == "hold") {
    step.kind = Kind::Hold;
    reader.expectKeys(value, "hold", {"ticks"}, {"keys"});
    step.ticks = reader.integer<std::uint64_t>(reader.field(value, "ticks"));
    if (const core::YamlNode* keys = value.find("keys")) {
      for (const core::YamlNode& key : reader.sequence(*keys, "keys")) {
        step.keys.push_back(reader.text(key));
        if (!keyNamed(step.keys.back())) {
          reader.fail(key, "unknown key");
        }
      }
    }
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
  } else if (kind == "click" || kind == "hover") {
    step.kind = kind == "click" ? Kind::Click : Kind::Hover;
    if (value.isMapping()) {
      reader.expectKeys(value, kind, {}, {"element", "window"});
      if (const core::YamlNode* element = value.find("element")) {
        step.text = reader.text(*element);
      } else if (const core::YamlNode* window = value.find("window")) {
        step.point = readPoint(reader, *window, "window");
        step.windowPoint = true;
      } else {
        reader.fail(value, std::format("{} takes element or window", kind));
      }
    } else if (step.kind == Kind::Hover) {
      reader.fail(value, "hover takes element or window");
    } else {
      step.point = readPoint(reader, value, "click");
    }
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
  // when: its conditions did not hold, so its block is skipped.
  bool skipBlock = false;
  std::uint64_t startTick = 0;
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
  double lastNoticeConfirm = -1.0;
  // The time walked before an encounter on the way, while it lasts (negative: none).
  double walked = -1.0;
  double lastAnswer = -1.0;
  // walk_to with `encounters: win`: the running encounter's fight was told to end won.
  bool encounterWon = false;
  // win_after: when the fight on screen showed (negative: none), and whether it was ended.
  double battleSince = -1.0;
  bool battleWon = false;
  // menu
  double lastTry = -1.0;
  // menu_select on a page with tabs: the tabs stepped to look for the entry.
  int tabSteps = 0;
  // resize and fullscreen
  rhi::Extent lastExtent{};
  int stableFrames = 0;
  bool extentMoved = false;
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
                    {"gamepad", "timeout", "main_menu", "boot_screen", "language_first",
                     "new_game_rules", "new_game_scene", "walk_control", "new_game", "names",
                     "load_save", "movies", "text_fit"});
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
  if (const core::YamlNode* boot = document->find("boot_screen")) {
    route.bootScreen = reader.boolean(*boot);
    if (route.bootScreen && !route.mainMenu) {
      reader.fail(*boot, "boot_screen needs main_menu: true");
    }
  }
  if (const core::YamlNode* language = document->find("language_first")) {
    route.languageFirst = reader.boolean(*language);
    if (route.languageFirst && !route.bootScreen) {
      reader.fail(*language, "language_first needs boot_screen: true");
    }
  }
  if (const core::YamlNode* rules = document->find("new_game_rules")) {
    route.newGameRules = reader.boolean(*rules);
    if (route.newGameRules && !route.mainMenu) {
      reader.fail(*rules, "new_game_rules needs main_menu: true");
    }
  }
  if (const core::YamlNode* scene = document->find("new_game_scene")) {
    route.newGameScene = reader.boolean(*scene);
    if (route.newGameScene && !route.mainMenu) {
      reader.fail(*scene, "new_game_scene needs main_menu: true");
    }
  }
  if (const core::YamlNode* walk = document->find("walk_control")) {
    const std::string control = reader.text(*walk);
    if (control == "mouse" || control == "keys") {
      route.walksWithMouse = control == "mouse";
    } else {
      reader.fail(*walk, "walk_control is keys or mouse");
    }
  }
  if (const core::YamlNode* movies = document->find("movies")) {
    route.movies = reader.boolean(*movies);
  }
  if (const core::YamlNode* textFit = document->find("text_fit")) {
    route.textFit = reader.boolean(*textFit);
  }
  if (const core::YamlNode* newGame = document->find("new_game")) {
    route.newGameChain = newGameChainNamed(reader.text(*newGame));
    if (!route.newGameChain) {
      reader.fail(*newGame, "new_game must be original or seeded");
    }
  }
  if (const core::YamlNode* names = document->find("names")) {
    if (reader.expectKeys(*names, "names", {}, {"player", "partner"})) {
      session::NewGameChoice choice;
      if (const core::YamlNode* player = names->find("player")) {
        choice.tamerName = reader.text(*player);
      }
      if (const core::YamlNode* partner = names->find("partner")) {
        choice.partnerName = reader.text(*partner);
      }
      route.names = std::move(choice);
    }
  }
  if (const core::YamlNode* save = document->find("load_save")) {
    route.loadSave = std::filesystem::path{reader.text(*save)};
    if (route.mainMenu || route.newGameChain) {
      reader.fail(*save, "load_save starts a saved game: no main_menu or new_game with it");
    }
    if (route.names) {
      reader.fail(*save, "load_save keeps the save's names: no names with it");
    }
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
  if (parsed->loadSave && parsed->loadSave->is_relative()) {
    parsed->loadSave = route.parent_path() / *parsed->loadSave;
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
  ticks_ = ticks;
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
      progress_->startTick = ticks;
      progress_->mapSerial = host.scene != nullptr ? host.scene->mapSerial() : 0;
    }
    // A step that is done counts even past its timeout: the press that starts a new game ends the
    // step only once its release has gone through, and the load in between can run long.
    if (host.scene == nullptr &&
        (step.kind == Kind::WalkTo ||
         (step.kind == Kind::Click && step.text.empty() && !step.windowPoint))) {
      failStep(messages::NoGame);
    } else if (runStep(host, step, now)) {
      finishStep(now);
    } else if (!finished() && now - progress_->start > step.timeout && step.kind != Kind::Wait &&
               step.kind != Kind::Hold) {
      // An expected setting says what it read, so a failed run tells the value it was left at.
      const std::string read =
          step.kind == Kind::ExpectSetting
              ? std::format("; {} is \"{}\"", step.name, settingText(host, step.name))
              : std::string{};
      failStep(
          std::format("timed out after {:.0f} s; {}{}", step.timeout, where(host.scene), read));
    }
    runActions(ticks, events);
  }
  if (!finished() && stepIndex_ >= route_.steps.size() && actions_.empty()) {
    result_ = textFailures_.empty() ? std::string{}
                                    : std::format("{} text check(s) failed, the first: {}",
                                                  textFailures_.size(), textFailures_.front());
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

int Autotest::ticksThisFrame(int ticks) const {
  if (finished() || stepIndex_ >= route_.steps.size()) {
    return ticks;
  }
  const AutotestStep& step = route_.steps[stepIndex_];
  if (step.kind != Kind::WalkTo || deviceOf(step) == AutotestDevice::Mouse) {
    return ticks;
  }
  return std::min(ticks, SteeredTicks);
}

void Autotest::finishStep(double now) {
  const AutotestStep& step = route_.steps[stepIndex_];
  const double seconds = now - progress_->start;
  std::string line =
      std::format("[{}/{}] {}{}{}: done in {:.2f} s", stepIndex_ + 1, route_.steps.size(),
                  kindName(step.kind), step.text.empty() ? "" : " ", step.text, seconds);
  core::logInfo("autotest: {}", line);
  stepLog_.push_back(std::move(line));
  const std::size_t skipped = progress_->skipBlock ? step.block : 0;
  progress_.reset();
  stepIndex_ += 1 + skipped;
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
  case Kind::Hover:
    return clickStep(host, step);
  case Kind::Script:
    return scriptStep(host, step);
  case Kind::Remember:
    return rememberStep(host, step);
  case Kind::ExpectChange:
    return expectChangeStep(host, step);
  case Kind::ScriptIf:
    return scriptIfStep(host, step);
  case Kind::When:
    return whenStep(host, step);
  case Kind::ExpectTextFits:
    return textFitStep(host, step, now);
  case Kind::ExpectClear:
    return clearStep(host, step, now);
  case Kind::Hold:
    return holdStep();
  }
  return true;
}

bool Autotest::holdStep() {
  if (!holdEnds_.contains(stepIndex_)) {
    // A run of hold steps starts at the next tick and is laid out tick for tick, so the frames'
    // length cannot stretch one hold into the next.
    holds_.clear();
    holdEnds_.clear();
    std::uint64_t from = ticks_;
    for (std::size_t index = stepIndex_;
         index < route_.steps.size() && route_.steps[index].kind == Kind::Hold; ++index) {
      const AutotestStep& held = route_.steps[index];
      ScheduledHold hold{.from = from, .to = from + held.ticks, .keys = {}};
      for (const std::string& name : held.keys) {
        hold.keys.push_back(keyNamed(name).value_or(platform::KeyCode{0}));
      }
      from = hold.to;
      holdEnds_.emplace(index, from);
      holds_.push_back(std::move(hold));
    }
  }
  return ticks_ >= holdEnds_.at(stepIndex_);
}

void Autotest::feedTick(std::uint64_t tick, input::InputState& input) {
  if (holds_.empty() && tickKeys_.empty()) {
    return;
  }
  std::vector<platform::KeyCode> wanted;
  const auto hold = std::ranges::find_if(
      holds_, [tick](const ScheduledHold& held) { return held.from <= tick && tick < held.to; });
  if (hold != holds_.end()) {
    wanted = hold->keys;
  } else if (!holds_.empty() && tick >= holds_.back().to) {
    holds_.clear();
  }
  for (const platform::KeyCode code : std::vector{tickKeys_}) {
    if (std::ranges::find(wanted, code) == wanted.end()) {
      input.handle(platform::KeyChanged{.key = code, .down = false, .repeat = false});
      std::erase(tickKeys_, code);
    }
  }
  for (const platform::KeyCode code : wanted) {
    if (std::ranges::find(tickKeys_, code) == tickKeys_.end()) {
      input.handle(platform::KeyChanged{.key = code, .down = true, .repeat = false});
      tickKeys_.push_back(code);
    }
  }
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

bool Autotest::whenStep(const AutotestHost& host, const AutotestStep& step) {
  if (const std::string unmet = unmetCondition(host, step.until); !unmet.empty()) {
    core::logInfo("{}",
                  core::formatMessage(messages::WhenNotRun, stepIndex_ + 1, step.block, unmet));
    progress_->skipBlock = true;
  }
  return true;
}

bool Autotest::waitStep(const AutotestHost& host, const AutotestStep& step, double now) {
  const double waited = now - progress_->start;
  if (step.confirm && host.ui != nullptr) {
    // As a player through a long scene: each page of the story's textbox and each line of a
    // fight's end box (battle_ui.c: handleBattleEndBox) takes a confirm; the fight itself plays.
    const bool battle = host.ui->isVisible(BattleDocument);
    const bool endBox = battle && settingText(host, "battle.phase") == "end";
    // A selection (its cursor shows) is the route's to answer, unless it asks for `answer`:
    // confirm takes the entry under the cursor.
    const std::string cursor = settingText(host, "story_textbox.cursor_row");
    const bool selecting = cursor != "-1" && cursor != "-";
    const bool textbox =
        !battle && (!selecting || step.answer) && host.ui->isVisible(StoryTextboxDocument);
    if ((endBox || textbox) && now - progress_->lastAnswer > AnswerSeconds) {
      progress_->lastAnswer = now;
      const AutotestDevice device = deviceOf(step);
      if (device == AutotestDevice::Mouse && textbox && host.renderer != nullptr) {
        // With the mouse a page takes a click on the scene above the box, as a player clicks:
        // never a walk order while the box holds the player.
        const rhi::Extent extent = host.renderer->targetExtent();
        constexpr float AboveTheBox = 0.3F;
        clickAt({static_cast<float>(extent.width) / 2.0F,
                 static_cast<float>(extent.height) * AboveTheBox});
      } else {
        tapAction(device == AutotestDevice::Gamepad ? device : AutotestDevice::Keyboard, "confirm");
      }
    }
    winLongFight(host, step, now);
  }
  if (waited < step.seconds || ticks_ - progress_->startTick < step.ticks) {
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
  // A tournament's step cell (call_routine 6) takes the player to the arena: its board or its
  // first match shows instead.
  if (step.stopOnScene && (field.heldByScript() || host.ui->isVisible(TournamentBracketDocument) ||
                           host.ui->isVisible(BattleDocument))) {
    releaseMovement();
    return true;
  }
  // The story lock keeps the exit closed and says so in the dialog: the player stays on the map.
  if (step.stopOnClosed && host.ui->isVisible("dialog")) {
    releaseMovement();
    return true;
  }
  // Free exploration asks where to go: the route answers in the chooser (wait for it shown, then
  // menu_select). The chooser of another cell on the way (the path steps back onto the cell the
  // player stayed on) is declined, as a player who stays, and the walk goes on.
  if (const auto& choice = field.destinationChoice()) {
    releaseMovement();
    if (!step.trigger || choice->cell == *step.trigger) {
      return true;
    }
    if (now - progress.lastAnswer > AnswerSeconds) {
      progress.lastAnswer = now;
      tapAction(device == AutotestDevice::Gamepad ? device : AutotestDevice::Keyboard, "cancel");
    }
    return false;
  }
  // An encounter on the way, which comes when timing says (a wild one walks up, talks and starts a
  // fight): with `encounters`, the walk stops, confirms the dialogue's pages, lets the fight play
  // out (pressing through its end box), and plans again from where the player is after it (its
  // drop's notice is confirmed below).
  // Its time stays out of the timeout.
  if (step.encounters && (field.heldByScript() || host.ui->isVisible(BattleDocument))) {
    releaseMovement();
    if (progress.walked < 0.0) {
      progress.walked = now - progress.start;
      core::logInfo("autotest: an encounter at ({:.0f}, {:.0f}); waiting it out", position.x,
                    position.z);
    }
    progress.start = now - progress.walked;
    // The fight's end box types each of its lines on a press (battle_ui.c: handleBattleEndBox).
    const bool endBox =
        host.ui->isVisible(BattleDocument) && settingText(host, "battle.phase") == "end";
    const bool textbox =
        !host.ui->isVisible(BattleDocument) && host.ui->isVisible(StoryTextboxDocument);
    if (step.winEncounters && !progress.encounterWon && host.scripts != nullptr &&
        host.ui->isVisible(BattleDocument) && settingText(host, "battle.phase") == "fight") {
      progress.encounterWon = true;
      if (auto ran = host.scripts->runString("battle_win(\"\")", "autotest"); !ran) {
        failStep(ran.error().message);
        return false;
      }
    }
    if ((endBox || textbox) && now - progress.lastAnswer > AnswerSeconds) {
      progress.lastAnswer = now;
      tapAction(device == AutotestDevice::Gamepad ? device : AutotestDevice::Keyboard, "confirm");
    }
    return false;
  }
  if (progress.walked >= 0.0) {
    core::logInfo("autotest: the encounter is over; planning again");
    progress.walked = -1.0;
    progress.encounterWon = false;
    progress.lastPosition = position;
    progress.lastProgress = now;
    progress.lastClick = -1.0;
    plan();
  }
  // A ground item on the way opens its pickup notice and the field waits (the original
  // checkItemPickup): the walk confirms it, as a player would, and goes on.
  if (host.ui->isVisible(FieldItemNoticeDocument)) {
    releaseMovement();
    progress.lastProgress = now;
    if (now - progress.lastNoticeConfirm > NoticeConfirmSeconds) {
      progress.lastNoticeConfirm = now;
      core::logInfo("autotest: a pickup notice on the way; confirming it");
      if (device == AutotestDevice::Gamepad) {
        button(platform::GamepadButton::South, true);
        button(platform::GamepadButton::South, false, TapTicks);
      } else {
        key(input::keys::Return, true);
        key(input::keys::Return, false, TapTicks);
      }
    }
    return false;
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
    // A row of another tab of a page with tabs (the options) is not there until its tab shows:
    // the key glyph after the tabs shows the next one.
    constexpr int MouseTabTries = 4;
    const auto nextTab = host.ui->elementCenter(menu, "tabs-next-key");
    if (!center && nextTab && host.ui->elementShown(menu, "tabs-next-key") &&
        progress.attempts < MouseTabTries) {
      clickAt({nextTab->x, nextTab->y});
      ++progress.attempts;
      return false;
    }
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
  // (the battle's commands). Enough downs for the longest page, a tab of the pause menu's options.
  // A page with tabs (an element menu-tabs on screen: the options) holds only its shown tab's rows:
  // while the entry is not there it steps to the next tab, then walks the tab up and down; it never
  // presses right or left, which change a row.
  constexpr int DownTries = 14;
  constexpr int UpTries = 24;
  constexpr int RightTries = 36;
  constexpr int MaxTries = 48;
  constexpr int MaxTabSteps = 4;
  const bool tabbed = host.ui->elementShown(menu, "menu-tabs");
  if (tabbed && !host.ui->elementCenter(menu, step.text)) {
    if (progress.tabSteps >= MaxTabSteps) {
      failStep("no tab of the page has it");
      return false;
    }
    tapAction(device, "tab_next");
    ++progress.tabSteps;
    progress.attempts = 0;
    return false;
  }
  if (progress.attempts >= (tabbed ? UpTries : MaxTries)) {
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

void Autotest::winLongFight(const AutotestHost& host, const AutotestStep& step, double now) {
  Progress& progress = *progress_;
  const bool fighting =
      host.ui->isVisible(BattleDocument) && settingText(host, "battle.phase") != "end";
  if (!step.winAfter || !fighting) {
    progress.battleSince = -1.0;
    progress.battleWon = false;
    return;
  }
  if (progress.battleSince < 0.0) {
    progress.battleSince = now;
  }
  if (progress.battleWon || now - progress.battleSince < *step.winAfter ||
      host.scripts == nullptr) {
    return;
  }
  progress.battleWon = true;
  core::logInfo("autotest: the fight lasted {:.0f} s; ending it with battle_win", *step.winAfter);
  if (auto ran = host.scripts->runString("battle_win(\"\")", "autotest"); !ran) {
    failStep(ran.error().message);
  }
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

bool Autotest::textFitStep(const AutotestHost& host, const AutotestStep& step, double now) {
  if (host.ui == nullptr) {
    failStep("the game has no UI");
    return false;
  }
  std::vector<ui::Ui::TextOverflow> overflows = host.ui->overflowingTexts();
  std::erase_if(overflows, [&step](const ui::Ui::TextOverflow& overflow) {
    return step.text != AllDocuments && overflow.document != step.text;
  });
  if (overflows.empty()) {
    return true;
  }
  if (now - progress_->start < step.timeout) {
    return false;
  }
  reportTextOverflows(std::move(overflows));
  return true;
}

bool Autotest::clearStep(const AutotestHost& host, const AutotestStep& step, double now) {
  if (host.ui == nullptr) {
    failStep("the game has no UI");
    return false;
  }
  // "document" or "document#element": one part of a document, checked against the rest of it too.
  const std::size_t hash = step.text.find('#');
  const std::string_view document = std::string_view{step.text}.substr(0, hash);
  const std::string_view element =
      hash == std::string::npos ? std::string_view{} : std::string_view{step.text}.substr(hash + 1);
  if (!host.ui->isVisible(document)) {
    failStep(std::format("{} is not shown", document));
    return false;
  }
  const std::vector<ui::Ui::Rect> mine =
      element.empty() ? host.ui->drawnRectsIn(document) : host.ui->drawnRectsOf(document, element);
  if (!element.empty() && mine.empty()) {
    if (now - progress_->start < step.timeout) {
      return false;
    }
    failStep(std::format("{} is not shown", step.text));
    return false;
  }
  const std::vector<ui::Ui::Rect> others = element.empty()
                                               ? host.ui->drawnRects(document)
                                               : host.ui->drawnRectsAround(document, element);
  std::size_t over = 0;
  std::optional<std::pair<ui::Ui::Rect, ui::Ui::Rect>> first;
  for (const ui::Ui::Rect& rect : mine) {
    for (const ui::Ui::Rect& other : others) {
      if (rect.left < other.right && other.left < rect.right && rect.top < other.bottom &&
          other.top < rect.bottom) {
        ++over;
        if (!first) {
          first.emplace(rect, other);
        }
      }
    }
  }
  if (over == 0) {
    return true;
  }
  if (now - progress_->start < step.timeout) {
    return false;
  }
  const auto& [rect, other] = *first;
  core::logError("autotest: step {}: {} lies over another document {} time(s), first ({:.0f}, "
                 "{:.0f})-({:.0f}, {:.0f}) over ({:.0f}, {:.0f})-({:.0f}, {:.0f})",
                 stepIndex_ + 1, step.text, over, rect.left, rect.top, rect.right, rect.bottom,
                 other.left, other.top, other.right, other.bottom);
  textFailures_.push_back(
      std::format("step {}: {} lies over another document", stepIndex_ + 1, step.text));
  return true;
}

void Autotest::reportTextOverflows(std::vector<ui::Ui::TextOverflow> overflows) {
  if (overflows.empty()) {
    return;
  }
  for (const ui::Ui::TextOverflow& overflow : overflows) {
    core::logError("autotest: step {}: in {}, '{}' ({}) is {} by {:.1f} dp", stepIndex_ + 1,
                   overflow.document, overflow.text, overflow.element, overflow.box,
                   overflow.overflow);
  }
  const ui::Ui::TextOverflow& first = overflows.front();
  textFailures_.push_back(std::format("step {}: {} text(s) do not fit, as '{}' in {}",
                                      stepIndex_ + 1, overflows.size(), first.text,
                                      first.document));
}

bool Autotest::screenshotStep(const AutotestHost& host, const AutotestStep& step) {
  Progress& progress = *progress_;
  if (route_.textFit && !progress.shot && host.ui != nullptr) {
    reportTextOverflows(host.ui->overflowingTexts());
  }
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
      const std::filesystem::path path =
          folder / std::format("{}-{}x{}.png", stem, captured->width, captured->height);
      if (auto written = writeScreenshot(std::move(*captured), path); !written) {
        *state = 2;
        *message = written.error().message;
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
    progress.extentMoved = true;
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
      if (progress.extentMoved || now - progress.start <= step.timeout / 2.0) {
        return false;
      }
      // An X display with no window manager (Xvfb) ignores the request: the window neither
      // changes mode nor size. The step is skipped there, as --skip-steps fullscreen would.
      std::string note =
          std::format("fullscreen {} skipped: the window manager did not apply it (window {}x{})",
                      step.flag ? "on" : "off", extent.width, extent.height);
      core::logWarning("autotest: {}", note);
      skipped_.push_back(std::move(note));
      return true;
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
  std::optional<glm::vec2> window;
  if (!step.text.empty()) {
    const std::size_t hash = step.text.find('#');
    const auto center = hash == std::string::npos
                            ? std::nullopt
                            : host.ui->elementCenter(std::string_view{step.text}.substr(0, hash),
                                                     std::string_view{step.text}.substr(hash + 1));
    if (!center) {
      failStep("no such element");
      return false;
    }
    window = glm::vec2{center->x, center->y};
  } else if (step.windowPoint) {
    window = step.point;
  } else {
    const glm::vec2 point = step.point.value_or(glm::vec2{0.0F});
    if (!walksWithMouse()) {
      // The keys walk the player, so a click would not: the step gives the walk order itself.
      walkOrder_ = glm::vec3{point.x, 0.0F, point.y};
      return true;
    }
    window = host.scene->worldToWindow({point.x, 0.0F, point.y});
  }
  if (!window) {
    failStep("the point is not in view");
    return false;
  }
  if (step.kind == Kind::Hover) {
    actions_.push_back(
        {.afterTicks = 0, .run = [at = *window](std::vector<platform::Event>& events) {
           events.emplace_back(platform::MouseMoved{.x = at.x, .y = at.y});
         }});
    actions_.push_back({.afterTicks = TapTicks, .run = nullptr});
  } else {
    clickAt(*window);
  }
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
  constexpr std::array<Control, 10> Controls{{
      {.action = "menu", .key = keys::Tab, .button = GamepadButton::North},
      {.action = "pause", .key = keys::P, .button = GamepadButton::Start},
      {.action = "confirm", .key = keys::Return, .button = GamepadButton::South},
      {.action = "cancel", .key = keys::LeftCtrl, .button = GamepadButton::East},
      {.action = "up", .key = keys::Up, .button = GamepadButton::DpadUp},
      {.action = "down", .key = keys::Down, .button = GamepadButton::DpadDown},
      {.action = "left", .key = keys::Left, .button = GamepadButton::DpadLeft},
      {.action = "right", .key = keys::Right, .button = GamepadButton::DpadRight},
      {.action = "tab_previous", .key = keys::Q, .button = GamepadButton::LeftShoulder},
      {.action = "tab_next", .key = keys::E, .button = GamepadButton::RightShoulder},
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
      button(platform::GamepadButton::RightShoulder, walk);
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
    button(platform::GamepadButton::RightShoulder, false);
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
