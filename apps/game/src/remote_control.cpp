#include "remote_control.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <iterator>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>

#include "device_names.hpp"
#include "place_command.hpp"

namespace dwe::app {

namespace {

using Kind = ControlRequest::Kind;

struct CommandName {
  std::string_view name;
  Kind kind;
};

constexpr std::array Commands{
    CommandName{"status", Kind::Status},
    CommandName{"state", Kind::State},
    CommandName{"press", Kind::Press},
    CommandName{"hold", Kind::Hold},
    CommandName{"release", Kind::Release},
    CommandName{"pause", Kind::Pause},
    CommandName{"resume", Kind::Resume},
    CommandName{"advance", Kind::Advance},
    CommandName{"screenshot", Kind::Screenshot},
    CommandName{"script", Kind::Script},
    CommandName{"place", Kind::Place},
    CommandName{"battle", Kind::Battle},
    CommandName{"load", Kind::Load},
    CommandName{"trace", Kind::Trace},
    CommandName{"quit", Kind::Quit},
    CommandName{"rescan", Kind::Rescan},
    CommandName{"reload_map", Kind::ReloadMap},
    CommandName{"story_state", Kind::StoryState},
    CommandName{"overlay", Kind::Overlay},
    CommandName{"live", Kind::Live},
    CommandName{"preview_map", Kind::PreviewMap},
    CommandName{"preview_clear", Kind::PreviewClear},
    CommandName{"log_tail", Kind::LogTail},
    CommandName{"story_set", Kind::StorySet},
};

// A press with no length holds its keys for a tenth of a second.
constexpr std::uint64_t DefaultPressTicks = 6;
// An hour of ticks: a longer advance or press is a mistake of the caller.
constexpr std::uint64_t MaxTicks = 60ULL * 60 * 60;

std::string commandList() {
  std::string out;
  for (const CommandName& command : Commands) {
    out += out.empty() ? "" : ", ";
    out += command.name;
  }
  return out;
}

core::Result<std::string> textField(const JsonValue& request, std::string_view key) {
  const JsonValue* value = request.find(key);
  if (value == nullptr || value->text() == nullptr || value->text()->empty()) {
    return core::fail(std::format("'{}' must be a non-empty string", key));
  }
  return *value->text();
}

core::Result<std::uint64_t> ticksField(const JsonValue& request, std::uint64_t fallback) {
  const JsonValue* value = request.find("ticks");
  if (value == nullptr) {
    return fallback;
  }
  const double* number = value->number();
  if (number == nullptr || *number < 0.0 || *number != std::trunc(*number) ||
      *number > static_cast<double>(MaxTicks)) {
    return core::fail(std::format("'ticks' must be a whole number from 0 to {}", MaxTicks));
  }
  return static_cast<std::uint64_t>(*number);
}

// "exit": a map's exit, 0 to 9 (the warp triggers 110-119 lead to exits 0-9).
core::Result<std::optional<std::int32_t>> exitField(const JsonValue& request) {
  constexpr double LastExit = 9.0;
  const JsonValue* value = request.find("exit");
  if (value == nullptr || value->isNull()) {
    return std::nullopt;
  }
  const double* number = value->number();
  if (number == nullptr || *number < 0.0 || *number > LastExit || *number != std::trunc(*number)) {
    return core::fail("'exit' must be a whole number from 0 to 9");
  }
  return static_cast<std::int32_t>(*number);
}

// A whole number from 0 to `last` at `key`.
core::Result<std::uint64_t> wholeField(const JsonValue& value, std::string_view key, double last) {
  const double* number = value.number();
  if (number == nullptr || *number < 0.0 || *number > last || *number != std::trunc(*number)) {
    return core::fail(std::format("'{}' must be a whole number from 0 to {}", key, last));
  }
  return static_cast<std::uint64_t>(*number);
}

// story_set: {"kind": "trigger", "number": 12, "value": true} or {"kind": "var", "number": 3,
// "value": 7}. The story's state says whether the number is one it has.
core::Result<StoryWrite> storyWriteField(const JsonValue& request) {
  constexpr double LastNumber = 65535.0;
  constexpr double LastValue = 255.0;
  const JsonValue* kind = request.find("kind");
  const std::string* kindName = kind != nullptr ? kind->text() : nullptr;
  if (kindName == nullptr || (*kindName != "trigger" && *kindName != "var")) {
    return core::fail(R"('kind' must be "trigger" or "var")");
  }
  const JsonValue* number = request.find("number");
  if (number == nullptr) {
    return core::fail("'number' is missing");
  }
  auto which = wholeField(*number, "number", LastNumber);
  if (!which) {
    return std::unexpected{which.error()};
  }
  const JsonValue* value = request.find("value");
  if (value == nullptr) {
    return core::fail("'value' is missing");
  }
  StoryWrite write{
      .kind = StoryWrite::Kind::Variable, .number = static_cast<std::int32_t>(*which), .value = 0};
  if (*kindName == "trigger") {
    write.kind = StoryWrite::Kind::Trigger;
    if (const bool* on = value->boolean()) {
      write.value = *on ? 1 : 0;
      return write;
    }
    auto bit = wholeField(*value, "value", 1.0);
    if (!bit) {
      return core::fail("a trigger's 'value' must be true, false, 1 or 0");
    }
    write.value = static_cast<std::int32_t>(*bit);
    return write;
  }
  auto byte = wholeField(*value, "value", LastValue);
  if (!byte) {
    return std::unexpected{byte.error()};
  }
  write.value = static_cast<std::int32_t>(*byte);
  return write;
}

// "keys": ["right", "return"] or "right+return".
core::Result<void> readKeys(const JsonValue& request, ControlRequest& out, bool required) {
  const JsonValue* value = request.find("keys");
  std::vector<std::string> names;
  if (value == nullptr || value->isNull()) {
    if (required) {
      return core::fail("'keys' is missing");
    }
    return {};
  }
  if (const std::string* text = value->text()) {
    for (const auto part : std::views::split(*text, '+')) {
      if (!std::ranges::empty(part)) {
        names.emplace_back(std::string_view{part});
      }
    }
  } else if (const JsonArray* items = value->array()) {
    for (const JsonValue& item : *items) {
      if (item.text() == nullptr) {
        return core::fail("'keys' must be key names");
      }
      names.push_back(*item.text());
    }
  } else {
    return core::fail("'keys' must be a list of key names or 'a+b'");
  }
  if (required && names.empty()) {
    return core::fail("'keys' is empty");
  }
  for (std::string& name : names) {
    const auto code = keyNamed(name);
    if (!code) {
      return core::fail(std::format("unknown key '{}' (a-z, 0-9, return, escape, backspace, tab, "
                                    "space, left, right, up, down, left_shift, right_shift)",
                                    name));
    }
    out.keys.push_back(*code);
    out.keyNames.push_back(std::move(name));
  }
  return {};
}

// A text a script call takes between quotes: the place and battle arguments never need quotes,
// backslashes or line breaks, so none is let through.
core::Result<std::string> scriptArgument(std::string_view text) {
  if (std::ranges::any_of(text, [](char c) { return c == '"' || c == '\\' || c == '\n'; })) {
    return core::fail(
        std::format("'{}': quotes, backslashes and line breaks are not allowed", text));
  }
  return std::format("\"{}\"", text);
}

core::Result<ControlRequest> requestFrom(const JsonValue& request) {
  if (request.object() == nullptr) {
    return core::fail("a request is a JSON object");
  }
  const JsonValue* command = request.find("cmd");
  if (command == nullptr || command->text() == nullptr) {
    return core::fail(std::format("'cmd' is missing; one of {}", commandList()));
  }
  const auto known = std::ranges::find(Commands, *command->text(), &CommandName::name);
  if (known == Commands.end()) {
    return core::fail(std::format("unknown cmd '{}'; one of {}", *command->text(), commandList()));
  }
  ControlRequest out;
  out.kind = known->kind;
  const auto take = [&](core::Result<std::string> text) -> core::Result<void> {
    if (!text) {
      return std::unexpected{text.error()};
    }
    out.text = std::move(*text);
    return {};
  };
  const auto takePath = [&](std::string_view key) -> core::Result<void> {
    auto text = textField(request, key);
    if (!text) {
      return std::unexpected{text.error()};
    }
    out.path = std::filesystem::path{*text};
    return {};
  };
  core::Result<void> read;
  switch (out.kind) {
  case Kind::State:
    if (const JsonValue* path = request.find("path"); path != nullptr && !path->isNull()) {
      read = take(textField(request, "path"));
    }
    break;
  case Kind::Press: {
    read = readKeys(request, out, true);
    const auto ticks = ticksField(request, DefaultPressTicks);
    if (read && !ticks) {
      read = std::unexpected{ticks.error()};
    }
    out.ticks = ticks.value_or(0);
    if (read && out.ticks == 0) {
      read = core::fail("a press lasts at least one tick");
    }
    break;
  }
  case Kind::Hold:
    read = readKeys(request, out, true);
    break;
  case Kind::Release:
    read = readKeys(request, out, false);
    break;
  case Kind::Advance: {
    if (request.find("ticks") == nullptr) {
      read = core::fail("'ticks' is missing");
      break;
    }
    const auto ticks = ticksField(request, 0);
    read = ticks ? core::Result<void>{} : std::unexpected{ticks.error()};
    out.ticks = ticks.value_or(0);
    break;
  }
  case Kind::Screenshot:
    read = takePath("path");
    break;
  case Kind::Script:
    read = take(textField(request, "code"));
    break;
  case Kind::Place:
    read = take(textField(request, "text"));
    if (read) {
      if (auto placement = placementFromText(out.text); !placement) {
        read = std::unexpected{placement.error()};
      }
    }
    break;
  case Kind::Battle:
    read = take(textField(request, "target"));
    break;
  case Kind::Load: {
    const bool save = request.find("save") != nullptr;
    const bool scene = request.find("scene") != nullptr;
    if (save == scene) {
      read = core::fail("load takes 'save' (a save file) or 'scene' (a map id), one of them");
    } else {
      read = save ? takePath("save") : take(textField(request, "scene"));
    }
    break;
  }
  case Kind::Trace:
    if (const JsonValue* path = request.find("path"); path != nullptr && !path->isNull()) {
      read = takePath("path");
    }
    break;
  case Kind::ReloadMap: {
    auto exit = exitField(request);
    read = exit ? core::Result<void>{} : std::unexpected{exit.error()};
    out.exit = exit.value_or(std::nullopt);
    break;
  }
  case Kind::StoryState:
    if (const JsonValue* path = request.find("path"); path != nullptr && !path->isNull()) {
      read = take(textField(request, "path"));
    }
    break;
  case Kind::Overlay:
    if (const JsonValue* on = request.find("on"); on != nullptr && !on->isNull()) {
      if (on->boolean() == nullptr) {
        read = core::fail("'on' must be true or false");
      } else {
        out.on = *on->boolean();
      }
    }
    break;
  case Kind::PreviewMap: {
    read = take(textField(request, "map"));
    const JsonValue* patch = request.find("patch");
    if (read && (patch == nullptr || patch->text() == nullptr)) {
      read = core::fail("'patch' must be the patch's YAML text");
    } else if (read) {
      // An empty patch changes nothing: the map as the packs give it, still a preview.
      out.patch = patch->text()->empty() ? std::string{"{}"} : *patch->text();
    }
    if (const JsonValue* pack = request.find("pack"); read && pack != nullptr && !pack->isNull()) {
      auto named = textField(request, "pack");
      read = named ? core::Result<void>{} : std::unexpected{named.error()};
      out.pack = named.value_or(std::string{});
    }
    break;
  }
  case Kind::LogTail:
    if (const JsonValue* since = request.find("since"); since != nullptr && !since->isNull()) {
      auto seq = wholeField(*since, "since", 9007199254740992.0);
      read = seq ? core::Result<void>{} : std::unexpected{seq.error()};
      out.since = seq.value_or(0);
    }
    break;
  case Kind::StorySet: {
    auto write = storyWriteField(request);
    read = write ? core::Result<void>{} : std::unexpected{write.error()};
    out.story = write.value_or(StoryWrite{});
    break;
  }
  case Kind::Live:
  case Kind::PreviewClear:
  case Kind::Rescan:
  case Kind::Status:
  case Kind::Pause:
  case Kind::Resume:
  case Kind::Quit:
    break;
  }
  if (!read) {
    return core::fail(std::format("{}: {}", known->name, read.error().message));
  }
  return out;
}

// A number as JSON: the shortest text that reads back the same, 0 for what JSON cannot hold.
// Written with to_chars, as live writes a few hundred of them a call.
template <typename Number>
void appendNumber(std::string& out, Number value) {
  if constexpr (std::is_floating_point_v<Number>) {
    if (!std::isfinite(value)) {
      out += '0';
      return;
    }
  }
  std::array<char, 32> text{};
  const auto written = std::to_chars(text.data(), text.data() + text.size(), value);
  out.append(text.data(), written.ptr);
}

// {"x":..,"y":..,"z":..,"yaw":..}, the keys after the opening brace's (an NPC's id) given.
void appendPose(std::string& out, const ControlPose& pose) {
  out += R"("x":)";
  appendNumber(out, pose.x);
  out += R"(,"y":)";
  appendNumber(out, pose.y);
  out += R"(,"z":)";
  appendNumber(out, pose.z);
  out += R"(,"yaw":)";
  appendNumber(out, pose.yaw);
  out += '}';
}

void appendPose(std::string& out, const std::optional<ControlPose>& pose) {
  if (!pose) {
    out += "null";
    return;
  }
  out += '{';
  appendPose(out, *pose);
}

std::string optionalText(const std::optional<std::string>& text) {
  return text ? jsonString(*text) : std::string{"null"};
}

std::string namesJson(const std::vector<std::string>& names) {
  std::string out = "[";
  for (const std::string& name : names) {
    out += out.size() > 1 ? "," : "";
    out += jsonString(name);
  }
  out += ']';
  return out;
}

} // namespace

ParsedControlRequest parseControlRequest(std::string_view line) {
  auto parsed = parseJson(line);
  if (!parsed) {
    return {.id = JsonValue{nullptr}, .request = std::unexpected{parsed.error()}};
  }
  const JsonValue* id = parsed->find("id");
  return {.id = id != nullptr ? *id : JsonValue{nullptr}, .request = requestFrom(*parsed)};
}

RemoteControl::RemoteControl(ControlHost host, bool paused)
    : host_{std::move(host)}, paused_{paused} {}

void RemoteControl::reply(std::uint64_t client, std::string_view id, std::string_view fields) {
  replies_.push_back(
      {.client = client, .line = std::format(R"({{"id":{},"ok":true{}}})", id, fields)});
}

void RemoteControl::fail(std::uint64_t client, std::string_view id, std::string_view problem) {
  replies_.push_back(
      {.client = client,
       .line = std::format(R"({{"id":{},"ok":false,"error":{}}})", id, jsonString(problem))});
}

void RemoteControl::receive(std::uint64_t client, std::string_view line) {
  ParsedControlRequest parsed = parseControlRequest(line);
  const std::string id = writeJson(parsed.id);
  if (!parsed.request) {
    fail(client, id, parsed.request.error().message);
    return;
  }
  handle(client, id, *parsed.request);
}

void RemoteControl::forget(std::uint64_t client) {
  std::erase_if(waits_, [client](const TickWait& wait) { return wait.client == client; });
  std::erase_if(shots_,
                [client](const std::shared_ptr<Shot>& shot) { return shot->client == client; });
}

std::string RemoteControl::heldNames() const {
  return namesJson(heldNames_);
}

std::string RemoteControl::statusFields() const {
  const ControlStatus status = host_.status();
  std::string fields =
      std::format(R"(,"tick":{},"paused":{},"mode":{},"map":{},"held":{},"pending":{})", ticks_,
                  paused_, jsonString(status.mode), jsonString(status.map), heldNames(), pending_);
  if (status.textures) {
    fields += std::format(R"(,"textures":{})", *status.textures);
  }
  if (status.gpuBytes) {
    fields += std::format(R"(,"gpu_bytes":{})", *status.gpuBytes);
  }
  if (status.overlay) {
    fields += std::format(R"(,"overlay":{})", *status.overlay);
  }
  if (status.clients) {
    fields += std::format(R"(,"clients":{})", *status.clients);
  }
  fields += std::format(R"(,"preview":{})", optionalText(status.preview));
  return fields;
}

std::string RemoteControl::liveFields() const {
  // About 80 bytes an NPC.
  constexpr std::size_t FixedBytes = 320;
  constexpr std::size_t NpcBytes = 80;
  const ControlLive live = host_.live();
  std::string fields;
  fields.reserve(FixedBytes + (NpcBytes * live.npcs.size()));
  std::format_to(std::back_inserter(fields),
                 R"(,"tick":{},"paused":{},"mode":{},"map":{},"busy":{},"preview":{},"log":{},)",
                 ticks_, paused_, jsonString(live.mode), jsonString(live.map), live.busy,
                 optionalText(live.preview), live.log);
  fields += R"("player":)";
  appendPose(fields, live.player);
  fields += R"(,"partner":)";
  appendPose(fields, live.partner);
  fields += R"(,"npcs":[)";
  for (std::size_t index = 0; index < live.npcs.size(); ++index) {
    fields += index == 0 ? R"({"id":)" : R"(,{"id":)";
    appendNumber(fields, live.npcs[index].id);
    fields += ',';
    appendPose(fields, live.npcs[index].pose);
  }
  fields += R"(],"view":)";
  if (live.view) {
    fields += R"({"x":)";
    appendNumber(fields, live.view->x);
    fields += R"(,"y":)";
    appendNumber(fields, live.view->y);
    fields += R"(,"w":)";
    appendNumber(fields, live.view->width);
    fields += R"(,"h":)";
    appendNumber(fields, live.view->height);
    fields += '}';
  } else {
    fields += "null";
  }
  return fields;
}

void RemoteControl::advanceBy(std::uint64_t ticks) {
  pending_ = std::max(pending_, ticks);
}

void RemoteControl::handle(std::uint64_t client, const std::string& id,
                           const ControlRequest& request) {
  switch (request.kind) {
  case Kind::Status:
    reply(client, id, statusFields());
    return;
  case Kind::State: {
    const auto line = host_.state();
    if (!line) {
      fail(client, id, std::format("no game is being played (mode {})", host_.status().mode));
      return;
    }
    auto state = parseJson(*line);
    if (!state) {
      fail(client, id, state.error().message);
      return;
    }
    auto picked = selectJson(*state, request.text);
    if (!picked) {
      fail(client, id, picked.error().message);
      return;
    }
    reply(client, id, std::format(R"(,"tick":{},"state":{})", ticks_, writeJson(*picked)));
    return;
  }
  case Kind::Press: {
    const std::uint64_t from = presses_.empty() ? ticks_ : std::max(ticks_, presses_.back().to);
    const std::uint64_t to = from + request.ticks;
    presses_.push_back({.from = from, .to = to, .keys = request.keys});
    if (paused_) {
      advanceBy(to - ticks_);
    }
    waits_.push_back({.client = client,
                      .id = id,
                      .until = to,
                      .fields = std::format(R"(,"keys":{},"from":{},"ticks":{})",
                                            namesJson(request.keyNames), from, request.ticks)});
    return;
  }
  case Kind::Hold:
    for (std::size_t index = 0; index < request.keys.size(); ++index) {
      if (std::ranges::find(held_, request.keys[index]) == held_.end()) {
        held_.push_back(request.keys[index]);
        heldNames_.push_back(request.keyNames[index]);
      }
    }
    reply(client, id, std::format(R"(,"held":{})", heldNames()));
    return;
  case Kind::Release:
    if (request.keys.empty()) {
      held_.clear();
      heldNames_.clear();
    }
    for (const platform::KeyCode code : request.keys) {
      if (const auto found = std::ranges::find(held_, code); found != held_.end()) {
        heldNames_.erase(heldNames_.begin() + (found - held_.begin()));
        held_.erase(found);
      }
    }
    reply(client, id, std::format(R"(,"held":{})", heldNames()));
    return;
  case Kind::Pause:
    paused_ = true;
    pending_ = 0;
    reply(client, id, statusFields());
    return;
  case Kind::Resume:
    paused_ = false;
    pending_ = 0;
    reply(client, id, statusFields());
    return;
  case Kind::Advance:
    if (paused_) {
      pending_ += request.ticks;
    }
    waits_.push_back({.client = client,
                      .id = id,
                      .until = ticks_ + (paused_ ? pending_ : request.ticks),
                      .fields = std::format(R"(,"advanced":{})", request.ticks)});
    // Zero ticks: the reply goes out with this frame's.
    return;
  case Kind::Screenshot: {
    auto shot = std::make_shared<Shot>(Shot{.client = client, .id = id, .result = std::nullopt});
    shots_.push_back(shot);
    host_.screenshot(*request.path, [shot](core::Result<std::string> written) {
      shot->result = std::move(written);
    });
    return;
  }
  case Kind::Script:
  case Kind::Place:
  case Kind::Battle: {
    std::string code = request.text;
    if (request.kind != Kind::Script) {
      auto argument = scriptArgument(request.text);
      if (!argument) {
        fail(client, id, argument.error().message);
        return;
      }
      // "battle" is battle_trigger.hpp's BattleCommand, which brings the battle's headers along.
      const std::string_view call =
          request.kind == Kind::Place ? PlaceCommand : std::string_view{"battle"};
      code = std::format("{}({})", call, *argument);
    }
    if (auto ran = host_.runScript(code); !ran) {
      fail(client, id, ran.error().message);
      return;
    }
    reply(client, id, std::format(R"(,"ran":{})", jsonString(code)));
    return;
  }
  case Kind::Load:
    if (auto loaded = host_.load(request); !loaded) {
      fail(client, id, loaded.error().message);
      return;
    }
    reply(client, id, statusFields());
    return;
  case Kind::Trace:
    if (auto traced = host_.trace(request.path); !traced) {
      fail(client, id, traced.error().message);
      return;
    }
    reply(client, id,
          request.path ? std::format(R"(,"trace":{})", jsonString(request.path->string()))
                       : std::string{R"(,"trace":null)"});
    return;
  case Kind::Quit:
    reply(client, id, "");
    host_.quit();
    return;
  case Kind::Rescan:
    if (!host_.rescan) {
      fail(client, id, "rescan: this game cannot list its packs again");
      return;
    }
    if (auto rescanned = host_.rescan(); !rescanned) {
      fail(client, id, std::format("rescan: {}", rescanned.error().message));
      return;
    }
    reply(client, id, "");
    return;
  case Kind::ReloadMap: {
    if (!host_.reloadMap) {
      fail(client, id, "reload_map: this game cannot reload its map");
      return;
    }
    auto reloaded = host_.reloadMap(request.exit);
    if (!reloaded) {
      fail(client, id, std::format("reload_map: {}", reloaded.error().message));
      return;
    }
    reply(client, id,
          std::format(R"(,"map":{},"exit":{},"tick":{})", jsonString(reloaded->map),
                      reloaded->exit ? std::to_string(*reloaded->exit) : "null", ticks_));
    return;
  }
  case Kind::StoryState: {
    if (!host_.storyState) {
      fail(client, id, "story_state: this game has no story to show");
      return;
    }
    auto text = host_.storyState();
    if (!text) {
      fail(client, id, std::format("story_state: {}", text.error().message));
      return;
    }
    auto story = parseJson(*text);
    if (!story) {
      fail(client, id, story.error().message);
      return;
    }
    auto picked = selectJson(*story, request.text);
    if (!picked) {
      fail(client, id, picked.error().message);
      return;
    }
    reply(client, id, std::format(R"(,"tick":{},"story":{})", ticks_, writeJson(*picked)));
    return;
  }
  case Kind::Overlay:
    if (!host_.overlay) {
      fail(client, id, "overlay: this game has no map overlay");
      return;
    }
    reply(client, id,
          std::format(R"(,"overlay":{})", host_.overlay(request.on) ? "true" : "false"));
    return;
  case Kind::Live:
    if (!host_.live) {
      fail(client, id, "live: this game cannot say where everyone stands");
      return;
    }
    reply(client, id, liveFields());
    return;
  case Kind::PreviewMap: {
    if (!host_.previewMap) {
      fail(client, id, "preview_map: this game cannot preview a map");
      return;
    }
    auto previewed = host_.previewMap(request);
    if (!previewed) {
      fail(client, id, std::format("preview_map: {}", previewed.error().message));
      return;
    }
    reply(client, id,
          std::format(R"(,"map":{},"applied":{},"tick":{})", jsonString(previewed->map),
                      previewed->reentered ? R"("reentered")" : R"("collision")", ticks_));
    return;
  }
  case Kind::PreviewClear: {
    if (!host_.previewClear) {
      fail(client, id, "preview_clear: this game cannot preview a map");
      return;
    }
    auto cleared = host_.previewClear();
    if (!cleared) {
      fail(client, id, std::format("preview_clear: {}", cleared.error().message));
      return;
    }
    reply(client, id,
          std::format(R"(,"map":{},"cleared":{})", jsonString(cleared->map), cleared->cleared));
    return;
  }
  case Kind::LogTail: {
    if (!host_.logTail) {
      fail(client, id, "log_tail: this game keeps no log for the channel");
      return;
    }
    const ControlLogTail tail = host_.logTail(request.since);
    std::string fields = std::format(R"(,"next":{},"entries":[)", tail.next);
    for (std::size_t index = 0; index < tail.entries.size(); ++index) {
      const ControlLogEntry& entry = tail.entries[index];
      std::format_to(std::back_inserter(fields), R"({}{{"seq":{},"level":{},"text":{}}})",
                     index == 0 ? "" : ",", entry.seq, entry.error ? R"("error")" : R"("warning")",
                     jsonString(entry.text));
    }
    fields += ']';
    reply(client, id, fields);
    return;
  }
  case Kind::StorySet: {
    if (!host_.storySet) {
      fail(client, id, "story_set: this game has no story to write");
      return;
    }
    if (auto written = host_.storySet(request.story); !written) {
      fail(client, id, std::format("story_set: {}", written.error().message));
      return;
    }
    const bool trigger = request.story.kind == StoryWrite::Kind::Trigger;
    reply(client, id,
          std::format(R"(,"kind":{},"number":{},"value":{})", trigger ? R"("trigger")" : R"("var")",
                      request.story.number,
                      trigger ? std::string{request.story.value != 0 ? "true" : "false"}
                              : std::to_string(request.story.value)));
    return;
  }
  }
}

int RemoteControl::ticksThisFrame(int stepTicks) {
  if (!paused_) {
    return stepTicks;
  }
  const auto ticks = std::min<std::uint64_t>(pending_, AdvanceTicksPerFrame);
  pending_ -= ticks;
  return static_cast<int>(ticks);
}

void RemoteControl::beforeTick(std::uint64_t tick, std::vector<platform::KeyChanged>& changes) {
  while (!presses_.empty() && presses_.front().to <= tick) {
    presses_.pop_front();
  }
  std::vector<platform::KeyCode> wanted = held_;
  for (const Press& press : presses_) {
    if (press.from <= tick && tick < press.to) {
      for (const platform::KeyCode code : press.keys) {
        if (std::ranges::find(wanted, code) == wanted.end()) {
          wanted.push_back(code);
        }
      }
    }
  }
  for (const platform::KeyCode code : std::vector{down_}) {
    if (std::ranges::find(wanted, code) == wanted.end()) {
      changes.push_back({.key = code, .down = false, .repeat = false});
      std::erase(down_, code);
    }
  }
  for (const platform::KeyCode code : wanted) {
    if (std::ranges::find(down_, code) == down_.end()) {
      changes.push_back({.key = code, .down = true, .repeat = false});
      down_.push_back(code);
    }
  }
}

void RemoteControl::afterFrame(std::uint64_t ticksRun) {
  ticks_ = ticksRun;
  const auto done = std::ranges::stable_partition(
      waits_, [this](const TickWait& wait) { return wait.until > ticks_; });
  for (const TickWait& wait : std::ranges::subrange{done.begin(), done.end()}) {
    reply(wait.client, wait.id, std::format(R"({},"tick":{})", wait.fields, ticks_));
  }
  waits_.erase(done.begin(), done.end());
  std::erase_if(shots_, [this](const std::shared_ptr<Shot>& shot) {
    if (!shot->result) {
      return false;
    }
    if (*shot->result) {
      reply(shot->client, shot->id,
            std::format(R"(,"path":{},"tick":{})", jsonString(**shot->result), ticks_));
    } else {
      fail(shot->client, shot->id, shot->result->error().message);
    }
    return true;
  });
}

bool RemoteControl::idle() const {
  return paused_ && pending_ == 0;
}

std::vector<ControlReply> RemoteControl::takeReplies() {
  return std::exchange(replies_, {});
}

} // namespace dwe::app
