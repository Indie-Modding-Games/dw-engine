#include "remote_control.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <iterator>
#include <ranges>
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

void RemoteControl::reply(std::string_view id, std::string_view fields) {
  replies_.push_back(std::format(R"({{"id":{},"ok":true{}}})", id, fields));
}

void RemoteControl::fail(std::string_view id, std::string_view problem) {
  replies_.push_back(std::format(R"({{"id":{},"ok":false,"error":{}}})", id, jsonString(problem)));
}

void RemoteControl::receive(std::string_view line) {
  ParsedControlRequest parsed = parseControlRequest(line);
  const std::string id = writeJson(parsed.id);
  if (!parsed.request) {
    fail(id, parsed.request.error().message);
    return;
  }
  handle(id, *parsed.request);
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
  return fields;
}

void RemoteControl::advanceBy(std::uint64_t ticks) {
  pending_ = std::max(pending_, ticks);
}

void RemoteControl::handle(const std::string& id, const ControlRequest& request) {
  switch (request.kind) {
  case Kind::Status:
    reply(id, statusFields());
    return;
  case Kind::State: {
    const auto line = host_.state();
    if (!line) {
      fail(id, std::format("no game is being played (mode {})", host_.status().mode));
      return;
    }
    auto state = parseJson(*line);
    if (!state) {
      fail(id, state.error().message);
      return;
    }
    auto picked = selectJson(*state, request.text);
    if (!picked) {
      fail(id, picked.error().message);
      return;
    }
    reply(id, std::format(R"(,"tick":{},"state":{})", ticks_, writeJson(*picked)));
    return;
  }
  case Kind::Press: {
    const std::uint64_t from = presses_.empty() ? ticks_ : std::max(ticks_, presses_.back().to);
    const std::uint64_t to = from + request.ticks;
    presses_.push_back({.from = from, .to = to, .keys = request.keys});
    if (paused_) {
      advanceBy(to - ticks_);
    }
    waits_.push_back({.id = id,
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
    reply(id, std::format(R"(,"held":{})", heldNames()));
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
    reply(id, std::format(R"(,"held":{})", heldNames()));
    return;
  case Kind::Pause:
    paused_ = true;
    pending_ = 0;
    reply(id, statusFields());
    return;
  case Kind::Resume:
    paused_ = false;
    pending_ = 0;
    reply(id, statusFields());
    return;
  case Kind::Advance:
    if (paused_) {
      pending_ += request.ticks;
    }
    waits_.push_back({.id = id,
                      .until = ticks_ + (paused_ ? pending_ : request.ticks),
                      .fields = std::format(R"(,"advanced":{})", request.ticks)});
    // Zero ticks: the reply goes out with this frame's.
    return;
  case Kind::Screenshot: {
    auto shot = std::make_shared<Shot>(Shot{.id = id, .result = std::nullopt});
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
        fail(id, argument.error().message);
        return;
      }
      // "battle" is battle_trigger.hpp's BattleCommand, which brings the battle's headers along.
      const std::string_view call =
          request.kind == Kind::Place ? PlaceCommand : std::string_view{"battle"};
      code = std::format("{}({})", call, *argument);
    }
    if (auto ran = host_.runScript(code); !ran) {
      fail(id, ran.error().message);
      return;
    }
    reply(id, std::format(R"(,"ran":{})", jsonString(code)));
    return;
  }
  case Kind::Load:
    if (auto loaded = host_.load(request); !loaded) {
      fail(id, loaded.error().message);
      return;
    }
    reply(id, statusFields());
    return;
  case Kind::Trace:
    if (auto traced = host_.trace(request.path); !traced) {
      fail(id, traced.error().message);
      return;
    }
    reply(id, request.path ? std::format(R"(,"trace":{})", jsonString(request.path->string()))
                           : std::string{R"(,"trace":null)"});
    return;
  case Kind::Quit:
    reply(id, "");
    host_.quit();
    return;
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
    reply(wait.id, std::format(R"({},"tick":{})", wait.fields, ticks_));
  }
  waits_.erase(done.begin(), done.end());
  std::erase_if(shots_, [this](const std::shared_ptr<Shot>& shot) {
    if (!shot->result) {
      return false;
    }
    if (*shot->result) {
      reply(shot->id, std::format(R"(,"path":{},"tick":{})", jsonString(**shot->result), ticks_));
    } else {
      fail(shot->id, shot->result->error().message);
    }
    return true;
  });
}

bool RemoteControl::idle() const {
  return paused_ && pending_ == 0;
}

std::vector<std::string> RemoteControl::takeReplies() {
  return std::exchange(replies_, {});
}

} // namespace dwe::app
