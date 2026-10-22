#include <dwe/game/story/story_runner.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <format>
#include <map>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

using namespace dwe::game::story;

namespace {

constexpr std::uint32_t TicksPerFrame = 3;
constexpr SectionRef Talk{.script = 105, .section = 6};
constexpr SectionRef Shared{.script = 199, .section = 0x51};
constexpr SectionRef Step{.script = 105, .section = 0x33};

// A host whose sections play a fixed list of steps, one per resume, and that writes down what the
// runner asks.
class ScriptedHost final : public SectionHost {
public:
  void define(SectionRef section, std::vector<SectionStep> steps) {
    scripts_[key(section)] = std::move(steps);
  }

  dwe::core::Result<SectionHandle> start(SectionRef section) override {
    const auto script = scripts_.find(key(section));
    if (script == scripts_.end()) {
      return dwe::core::fail("no such section");
    }
    const SectionHandle handle = nextHandle_++;
    running_[handle] = {.ref = section, .steps = {script->second.begin(), script->second.end()}};
    log_.push_back(std::format("start {}", name(section)));
    return handle;
  }

  SectionStep resume(SectionHandle handle, ResumeValue value) override {
    Running& section = running_.at(handle);
    log_.push_back(
        std::format("resume {} {}", name(section.ref), value ? std::to_string(*value) : "nil"));
    if (section.steps.empty()) {
      return SectionFailed{.message = "resumed past its end"};
    }
    SectionStep step = section.steps.front();
    section.steps.pop_front();
    return step;
  }

  void close(SectionHandle handle) override {
    log_.push_back(std::format("close {}", name(running_.at(handle).ref)));
    running_.erase(handle);
  }

  [[nodiscard]] std::size_t open() const { return running_.size(); }
  [[nodiscard]] const std::vector<std::string>& log() const { return log_; }
  void clearLog() { log_.clear(); }

private:
  struct Running {
    SectionRef ref;
    std::deque<SectionStep> steps;
  };

  static std::uint32_t key(SectionRef section) {
    return (std::uint32_t{section.script} << 16U) | section.section;
  }
  static std::string name(SectionRef section) {
    return std::format("{}:{:#x}", section.script, section.section);
  }

  std::map<std::uint32_t, std::vector<SectionStep>> scripts_;
  std::map<SectionHandle, Running> running_;
  std::vector<std::string> log_;
  SectionHandle nextHandle_ = 1;
};

StoryRunner makeRunner(ScriptedHost& host) {
  auto runner = StoryRunner::create(host, TicksPerFrame);
  REQUIRE(runner);
  return std::move(*runner);
}

void runFrames(StoryRunner& runner, std::uint32_t frames) {
  for (std::uint32_t tick = 0; tick < frames * TicksPerFrame; ++tick) {
    runner.tick();
  }
}

Wait frames(std::uint32_t count, ResumeValue result = std::nullopt) {
  return FrameWait{.frames = count, .result = result};
}

} // namespace

TEST_CASE("a frame of the original lasts a whole number of ticks, at least one") {
  ScriptedHost host;
  CHECK_FALSE(StoryRunner::create(host, 0));
  CHECK(StoryRunner::create(host, 1));
}

TEST_CASE("a requested section runs its first order at the next frame, or at once when asked") {
  ScriptedHost host;
  host.define(Talk, {SectionEnded{}});
  StoryRunner runner = makeRunner(host);

  REQUIRE(runner.request(Talk, StartTiming::NextFrame) == RequestOutcome::Accepted);
  CHECK(runner.busy());
  runner.tick();
  runner.tick();
  CHECK(host.log().empty());
  runner.tick();
  CHECK(host.log() ==
        std::vector<std::string>{"start 105:0x6", "resume 105:0x6 nil", "close 105:0x6"});
  CHECK_FALSE(runner.busy());

  host.clearLog();
  REQUIRE(runner.request(Talk, StartTiming::Now) == RequestOutcome::Accepted);
  CHECK(host.log() ==
        std::vector<std::string>{"start 105:0x6", "resume 105:0x6 nil", "close 105:0x6"});
}

TEST_CASE(
    "one script at a time: a request while one runs or is about to start is dropped, not queued") {
  ScriptedHost host;
  host.define(Talk, {frames(2), SectionEnded{}});
  host.define(Step, {SectionEnded{}});
  StoryRunner runner = makeRunner(host);

  REQUIRE(runner.request(Talk, StartTiming::NextFrame) == RequestOutcome::Accepted);
  CHECK(runner.request(Step, StartTiming::Now) == RequestOutcome::Dropped);
  runFrames(runner, 1);
  CHECK(runner.request(Step, StartTiming::NextFrame) == RequestOutcome::Dropped);
  runFrames(runner, 2);
  CHECK_FALSE(runner.busy());
  CHECK(runner.request(Step, StartTiming::NextFrame) == RequestOutcome::Accepted);
  runFrames(runner, 1);
  CHECK(host.log().back() == "close 105:0x33");
}

TEST_CASE("a wait of n frames resumes in the nth frame after it, with the wait's result") {
  ScriptedHost host;
  host.define(Talk, {frames(3, 2), frames(1), SectionEnded{}});
  StoryRunner runner = makeRunner(host);
  REQUIRE(runner.request(Talk, StartTiming::Now) == RequestOutcome::Accepted);
  host.clearLog();

  runFrames(runner, 2);
  CHECK(host.log().empty());
  runFrames(runner, 1);
  CHECK(host.log() == std::vector<std::string>{"resume 105:0x6 2"});
  runFrames(runner, 1);
  CHECK(host.log().back() == "close 105:0x6");
  CHECK_FALSE(runner.busy());
}

TEST_CASE("a frame count of 0 waits one frame") {
  ScriptedHost host;
  host.define(Talk, {frames(0), SectionEnded{}});
  StoryRunner runner = makeRunner(host);
  REQUIRE(runner.request(Talk, StartTiming::Now) == RequestOutcome::Accepted);
  runFrames(runner, 1);
  CHECK_FALSE(runner.busy());
}

TEST_CASE(
    "a jump outside any subroutine replaces the section, so two scripts can jump to each other forever") {
  // Script 49 section 0x52 and scripts 199-202 section 0x33 of the disc do this in the original.
  ScriptedHost host;
  host.define(Talk, {frames(1), FileJump{.target = Shared}});
  host.define(Shared, {frames(1), FileJump{.target = Talk}});
  StoryRunner runner = makeRunner(host);
  REQUIRE(runner.request(Talk, StartTiming::Now) == RequestOutcome::Accepted);

  // Past the 8 frames of the original's script stack, which a stacking jump would fill.
  constexpr std::uint32_t Rounds = 24;
  for (std::uint32_t frame = 0; frame < 2 * Rounds; ++frame) {
    runFrames(runner, 1);
    REQUIRE(runner.busy());
    REQUIRE(runner.runningSections().size() == 1);
    REQUIRE(host.open() == 1);
  }
  CHECK(std::ranges::count(host.log(), std::string{"start 105:0x6"}) > Rounds);
}

TEST_CASE(
    "a jump from inside a subroutine waits under the target, which returns nil to it in the same frame") {
  ScriptedHost host;
  host.define(Talk, {FileJump{.target = Shared, .linkDepth = 1}, SectionEnded{}});
  host.define(Shared, {frames(1), SectionReturned{}});
  StoryRunner runner = makeRunner(host);

  REQUIRE(runner.request(Talk, StartTiming::Now) == RequestOutcome::Accepted);
  CHECK(runner.runningSections() == std::vector<SectionRef>{Talk, Shared});
  CHECK(host.log() == std::vector<std::string>{"start 105:0x6", "resume 105:0x6 nil",
                                               "start 199:0x51", "resume 199:0x51 nil"});
  host.clearLog();
  runFrames(runner, 1);
  CHECK(host.log() == std::vector<std::string>{"resume 199:0x51 nil", "close 199:0x51",
                                               "resume 105:0x6 nil", "close 105:0x6"});
  CHECK_FALSE(runner.busy());
}

TEST_CASE("a section reached by a jump that ends the script ends the section that jumped too") {
  ScriptedHost host;
  host.define(Talk, {FileJump{.target = Shared, .linkDepth = 2}, frames(1)});
  host.define(Shared, {SectionEnded{}});
  StoryRunner runner = makeRunner(host);
  REQUIRE(runner.request(Talk, StartTiming::Now) == RequestOutcome::Accepted);
  CHECK(host.log() == std::vector<std::string>{"start 105:0x6", "resume 105:0x6 nil",
                                               "start 199:0x51", "resume 199:0x51 nil",
                                               "close 199:0x51", "close 105:0x6"});
  CHECK(host.open() == 0);
  CHECK_FALSE(runner.busy());
}

TEST_CASE(
    "a jump deeper in subroutine calls than the engine's guard ends the script, and one at the guard does not") {
  ScriptedHost host;
  constexpr std::uint32_t Outer = 200;
  constexpr auto Inner = static_cast<std::uint32_t>(StoryRunner::MaxLinkDepth) - Outer;
  host.define(Talk, {FileJump{.target = Shared, .linkDepth = Outer}});
  host.define(Shared, {FileJump{.target = Step, .linkDepth = Inner}});
  host.define(Step, {frames(1)});
  StoryRunner runner = makeRunner(host);
  REQUIRE(runner.request(Talk, StartTiming::Now) == RequestOutcome::Accepted);
  CHECK(runner.runningSections() == std::vector<SectionRef>{Talk, Shared, Step});

  ScriptedHost deeper;
  deeper.define(Talk, {FileJump{.target = Shared, .linkDepth = Outer}});
  deeper.define(Shared, {FileJump{.target = Step, .linkDepth = Inner + 1}});
  deeper.define(Step, {frames(1)});
  StoryRunner overflowing = makeRunner(deeper);
  REQUIRE(overflowing.request(Talk, StartTiming::Now) == RequestOutcome::Accepted);
  CHECK_FALSE(overflowing.busy());
  CHECK(deeper.open() == 0);
  CHECK(std::ranges::count(deeper.log(), std::string{"start 105:0x33"}) == 0);
}

TEST_CASE("a jump to a missing section or a return with nothing to return to ends the script") {
  ScriptedHost host;
  host.define(Talk, {FileJump{.target = SectionRef{.script = 1, .section = 2}}});
  host.define(Shared, {FileJump{.target = Step}});
  host.define(Step, {SectionReturned{}});
  StoryRunner runner = makeRunner(host);

  REQUIRE(runner.request(Talk, StartTiming::Now) == RequestOutcome::Accepted);
  CHECK_FALSE(runner.busy());
  CHECK(host.open() == 0);

  REQUIRE(runner.request(Shared, StartTiming::Now) == RequestOutcome::Accepted);
  CHECK_FALSE(runner.busy());
  CHECK(host.open() == 0);
}

TEST_CASE("a section that fails ends the script") {
  ScriptedHost host;
  host.define(Talk, {SectionFailed{.message = "attempt to call a nil value"}});
  StoryRunner runner = makeRunner(host);
  REQUIRE(runner.request(Talk, StartTiming::Now) == RequestOutcome::Accepted);
  CHECK_FALSE(runner.busy());
  CHECK(host.open() == 0);
}

TEST_CASE("a section that never waits is stopped after the most resumes a frame allows") {
  ScriptedHost host;
  host.define(Shared, {FileJump{.target = Shared}});
  StoryRunner runner = makeRunner(host);
  REQUIRE(runner.request(Shared, StartTiming::Now) == RequestOutcome::Accepted);
  CHECK_FALSE(runner.busy());
  CHECK(host.open() == 0);
  CHECK(std::ranges::count(host.log(), std::string{"resume 199:0x51 nil"}) ==
        StoryRunner::MaxResumesPerFrame);
}
