#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/story/section_host.hpp>
#include <dwe/game/story/story_orders.hpp>
#include <dwe/game/story/story_ports.hpp>
#include <dwe/game/story/story_runner.hpp>
#include <dwe/script/api_registry.hpp>
#include <dwe/script/script_vm.hpp>
#include <dwe/session/story_lua.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace story_play {

using dwe::game::story::SectionRef;

inline constexpr std::int64_t PlayerEntity = 0xFD;
inline constexpr std::int64_t PartnerEntity = 0xFC;
inline constexpr dwe::game::story::StoryRules OriginalRules{.variableMax = 255};
inline constexpr std::int64_t OriginalTriggers = 800;
inline constexpr std::int64_t OriginalVariables = 256;
// How many frames a run may wait in all before it counts as cut.
inline constexpr std::size_t MaxTicks = 2'000'000;

inline constexpr std::chrono::milliseconds TestDeadline{60'000};
inline constexpr dwe::script::ScriptLimits TestLimits{.callDeadline = TestDeadline,
                                                      .hookDeadline = TestDeadline,
                                                      .hookTimePerFrame = TestDeadline,
                                                      .resumeDeadline = TestDeadline};

inline constexpr std::string_view LimitReached = "the run made too many calls";

// The error of a result, or nothing: doctest -s prints the messages of passing assertions too.
template <typename Result>
std::string errorText(const Result& result) {
  return result ? std::string{} : result.error().message;
}

// What a run did, in order: the state it wrote, the waits it made and the calls of the stubs.
struct Recorder {
  std::vector<std::string> events;
  std::size_t limit = std::numeric_limits<std::size_t>::max();
  // Whether a call went past the limit, also one that cannot fail, such as a sound.
  bool overflowed = false;

  bool record(std::string event) {
    if (events.size() >= limit) {
      overflowed = true;
      return false;
    }
    events.push_back(std::move(event));
    return true;
  }
};

class RecordedState final : public dwe::game::story::StoryStatePort {
public:
  explicit RecordedState(Recorder& recorder) : recorder_{&recorder} {}

  void reset(std::vector<bool> triggers, std::vector<std::int64_t> variables) {
    triggers_ = std::move(triggers);
    variables_ = std::move(variables);
  }

  [[nodiscard]] std::int64_t triggerCount() const override {
    return static_cast<std::int64_t>(triggers_.size());
  }
  [[nodiscard]] std::int64_t variableCount() const override {
    return static_cast<std::int64_t>(variables_.size());
  }
  [[nodiscard]] dwe::core::Result<bool> trigger(std::int64_t trigger) const override {
    if (trigger < 0 || trigger >= triggerCount()) {
      return dwe::core::fail(std::format("no trigger {}", trigger));
    }
    return static_cast<bool>(triggers_[static_cast<std::size_t>(trigger)]);
  }
  dwe::core::Result<void> setTrigger(std::int64_t trigger, bool set) override {
    if (!recorder_->record(std::format("trigger {} = {}", trigger, set ? 1 : 0))) {
      return dwe::core::fail(std::string{LimitReached});
    }
    triggers_.at(static_cast<std::size_t>(trigger)) = set;
    return {};
  }
  [[nodiscard]] dwe::core::Result<std::int64_t> variable(std::int64_t variable) const override {
    if (variable < 0 || variable >= variableCount()) {
      return dwe::core::fail(std::format("no variable {}", variable));
    }
    return variables_[static_cast<std::size_t>(variable)];
  }
  dwe::core::Result<void> setVariable(std::int64_t variable, std::int64_t value) override {
    if (!recorder_->record(std::format("var {} = {}", variable, value))) {
      return dwe::core::fail(std::string{LimitReached});
    }
    variables_.at(static_cast<std::size_t>(variable)) = value;
    return {};
  }

private:
  Recorder* recorder_;
  std::vector<bool> triggers_ = std::vector<bool>(OriginalTriggers);
  std::vector<std::int64_t> variables_ = std::vector<std::int64_t>(OriginalVariables);
};

// Records the story's sounds as the reference writes their calls.
class RecordedSound final : public dwe::game::story::SoundPort {
public:
  explicit RecordedSound(Recorder& recorder) : recorder_{&recorder} {}

  void playSound(std::int64_t slot, std::int64_t sound) override {
    static_cast<void>(recorder_->record(std::format("play_sound({},{})", slot, sound)));
  }

private:
  Recorder* recorder_;
};

// The numbers a seed gives, the same on the Lua side and in a reference.
class SeededRandom final : public dwe::game::story::StoryRandomPort {
public:
  void reset(std::uint32_t seed) { state_ = (seed * 2654435761U) + 1U; }

  std::int64_t next() override {
    state_ = (state_ * 1664525U) + 1013904223U;
    return static_cast<std::int64_t>((state_ >> 8U) % (RandomMax + 1));
  }

private:
  std::uint32_t state_ = 1;
};

struct Played {
  std::vector<std::string> events;
  std::string status; // "ended", "returned", "cut" or "error", as story_lua::Outcome
  std::string failure;
  std::uint32_t deepestLink = 0;
  std::size_t fileJumps = 0;
  // The most sections the host held open at once.
  std::size_t peakSections = 0;
};

// Records how the host's sections leave each resume; the status is how the last one left.
class RecordingHost final : public dwe::game::story::SectionHost {
public:
  RecordingHost(dwe::game::story::SectionHost& inner, Recorder& recorder)
      : inner_{&inner}, recorder_{&recorder} {}

  void reset() {
    outcome_ = Played{.events = {},
                      .status = "none",
                      .failure = {},
                      .deepestLink = 0,
                      .fileJumps = 0,
                      .peakSections = 0};
    liveSections_ = 0;
  }

  // Everything but the events, which the recorder holds.
  [[nodiscard]] const Played& outcome() const { return outcome_; }

  dwe::core::Result<dwe::game::story::SectionHandle> start(SectionRef section) override {
    auto handle = inner_->start(section);
    if (handle) {
      ++liveSections_;
      outcome_.peakSections = std::max(outcome_.peakSections, liveSections_);
    } else {
      outcome_.status = "error";
      outcome_.failure = handle.error().message;
    }
    return handle;
  }

  dwe::game::story::SectionStep resume(dwe::game::story::SectionHandle handle,
                                       dwe::game::story::ResumeValue value) override {
    using namespace dwe::game::story;
    SectionStep step = inner_->resume(handle, value);
    if (const auto* wait = std::get_if<Wait>(&step)) {
      std::string event;
      if (const auto* frames = std::get_if<FrameWait>(wait)) {
        event = std::format("delay({})", frames->frames);
      } else {
        const auto& jump = std::get<FileJump>(*wait);
        event = std::format("jump_to_file({},{})", jump.target.script, jump.target.section);
        outcome_.deepestLink = std::max(outcome_.deepestLink, jump.linkDepth);
        ++outcome_.fileJumps;
      }
      if (!recorder_->record(std::move(event))) {
        step = SectionFailed{.message = std::string{LimitReached}};
      }
    }
    if (recorder_->overflowed && !std::holds_alternative<SectionFailed>(step)) {
      step = SectionFailed{.message = std::string{LimitReached}};
    }
    if (std::holds_alternative<SectionEnded>(step)) {
      outcome_.status = "ended";
    } else if (std::holds_alternative<SectionReturned>(step)) {
      outcome_.status = "returned";
    } else if (const auto* failed = std::get_if<SectionFailed>(&step)) {
      outcome_.failure = failed->message;
      outcome_.status =
          failed->message.contains(LimitReached) || failed->message.contains("ran too long")
              ? "cut"
              : "error";
    }
    return step;
  }

  void close(dwe::game::story::SectionHandle handle) override {
    --liveSections_;
    inner_->close(handle);
  }

private:
  Played outcome_;
  std::size_t liveSections_ = 0;
  dwe::game::story::SectionHost* inner_;
  Recorder* recorder_;
};

// A VM with the story API of StoryLuaHost over a recorded state, and a runner per run. Not
// movable: the published functions point into it.
class StoryPlay {
public:
  explicit StoryPlay(const dwe::assets::VirtualFileSystem& files)
      : vm_{makeVm()}, host_{*vm_, files}, recording_{host_, recorder_} {
    REQUIRE(host_.publishApi(state_, orders_, sound_));
  }
  StoryPlay(const StoryPlay&) = delete;
  StoryPlay& operator=(const StoryPlay&) = delete;
  StoryPlay(StoryPlay&&) = delete;
  StoryPlay& operator=(StoryPlay&&) = delete;
  ~StoryPlay() = default;

  [[nodiscard]] dwe::script::ScriptVm& vm() const { return *vm_; }

  // Runs a section through a StoryRunner of one tick per frame until the script is done.
  Played play(SectionRef section, std::uint32_t seed) {
    recorder_.events.clear();
    recorder_.overflowed = false;
    random_.reset(seed);
    recording_.reset();
    auto runner = dwe::game::story::StoryRunner::create(recording_, 1);
    REQUIRE(runner.has_value());
    runner->request(section, dwe::game::story::StartTiming::Now);
    std::size_t ticks = 0;
    for (; runner->busy() && ticks < MaxTicks; ++ticks) {
      runner->tick();
    }
    Played played = recording_.outcome();
    played.events = recorder_.events;
    if (runner->busy()) {
      played.status = "cut";
    }
    return played;
  }

  [[nodiscard]] Recorder& recorder() { return recorder_; }
  [[nodiscard]] RecordedState& state() { return state_; }

private:
  static std::unique_ptr<dwe::script::ScriptVm> makeVm() {
    auto vm = dwe::script::ScriptVm::create(TestLimits);
    REQUIRE(vm.has_value());
    return std::move(*vm);
  }

  Recorder recorder_;
  RecordedState state_{recorder_};
  RecordedSound sound_{recorder_};
  SeededRandom random_;
  std::unique_ptr<dwe::script::ScriptVm> vm_;
  dwe::game::story::StoryOrders orders_{state_, random_, OriginalRules};
  dwe::session::StoryLuaHost host_;
  RecordingHost recording_;
};

// The text of a value as Lua's tostring writes it.
inline std::string textOf(const dwe::script::ApiValue& value) {
  return std::visit(
      [](const auto& held) -> std::string {
        using Held = std::decay_t<decltype(held)>;
        if constexpr (std::is_same_v<Held, std::monostate>) {
          return "nil";
        } else if constexpr (std::is_same_v<Held, bool>) {
          return held ? "true" : "false";
        } else if constexpr (std::is_same_v<Held, std::string>) {
          return held;
        } else {
          return std::format("{}", held);
        }
      },
      value);
}

// A stub of the test: records its call by name and gives back nil.
inline dwe::script::ApiFunction recordedCall(Recorder& recorder, std::string name,
                                             dwe::script::ApiValue result = {}) {
  return [&recorder, name = std::move(name), result = std::move(result)](
             const dwe::script::ApiCall& call) -> dwe::core::Result<dwe::script::ApiValue> {
    std::string event = name + "(";
    for (std::size_t index = 0; index < call.arguments().size(); ++index) {
      event += (index == 0 ? "" : ",") + textOf(call.arguments()[index]);
    }
    if (!recorder.record(event + ")")) {
      return dwe::core::fail(std::string{LimitReached});
    }
    return result;
  };
}

// Counts the log lines that contain a text, from the first call on.
inline std::size_t& loggedLinesWith(std::string_view text) {
  static std::deque<std::pair<std::string, std::size_t>> counters;
  const auto found = std::ranges::find(counters, text, &std::pair<std::string, std::size_t>::first);
  if (found != counters.end()) {
    return found->second;
  }
  if (counters.empty()) {
    dwe::core::addLogSink([](dwe::core::LogLevel /*level*/, std::string_view message) {
      for (auto& [wanted, count] : counters) {
        count += message.contains(wanted) ? 1 : 0;
      }
    });
  }
  return counters.emplace_back(std::string{text}, 0).second;
}

} // namespace story_play
