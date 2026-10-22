#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/story/story_runner.hpp>

#include <algorithm>
#include <numeric>
#include <ranges>
#include <type_traits>
#include <utility>
#include <variant>

#include "messages.hpp"

namespace dwe::game::story {

core::Result<StoryRunner> StoryRunner::create(SectionHost& host, std::uint32_t ticksPerFrame) {
  if (ticksPerFrame == 0) {
    return core::fail(std::string{messages::NoTicksPerFrame});
  }
  return StoryRunner{host, ticksPerFrame};
}

RequestOutcome StoryRunner::request(SectionRef section, StartTiming timing) {
  if (busy()) {
    const SectionRef running =
        pending_.value_or(sections_.empty() ? section : sections_.front().ref);
    core::logDebug("{}", messages::formatMessage(messages::RequestDropped, section.section,
                                                 section.script, running.script));
    return RequestOutcome::Dropped;
  }
  if (timing == StartTiming::Now) {
    startSection(section);
  } else {
    pending_ = section;
  }
  return RequestOutcome::Accepted;
}

void StoryRunner::tick() {
  ++ticksIntoFrame_;
  if (ticksIntoFrame_ < ticksPerFrame_) {
    return;
  }
  ticksIntoFrame_ = 0;
  runFrame();
}

std::vector<SectionRef> StoryRunner::runningSections() const {
  return sections_ | std::views::transform(&RunningSection::ref) | std::ranges::to<std::vector>();
}

void StoryRunner::runFrame() {
  if (const std::optional<SectionRef> section = std::exchange(pending_, std::nullopt)) {
    startSection(*section);
    return;
  }
  if (sections_.empty()) {
    return;
  }
  // script_interp.c: tickScript counts the wait down and, when it reaches 0, runs the next order in
  // the same frame.
  --framesToWait_;
  if (framesToWait_ > 0) {
    return;
  }
  run(std::exchange(waitResult_, std::nullopt));
}

void StoryRunner::startSection(SectionRef section) {
  auto handle = host_->start(section);
  if (!handle) {
    core::logError("{}", messages::formatMessage(messages::SectionMissing, section.script,
                                                 section.section, handle.error().message));
    return;
  }
  sections_.push_back({.ref = section, .handle = *handle});
  run(std::nullopt);
}

void StoryRunner::run(ResumeValue value) {
  std::optional<ResumeValue> next = value;
  std::uint32_t resumes = 0;
  while (next && !sections_.empty()) {
    if (resumes == MaxResumesPerFrame) {
      fail(messages::formatMessage(messages::NeverWaits, sections_.back().ref.script,
                                   sections_.back().ref.section, resumes));
      return;
    }
    ++resumes;
    next = follow(host_->resume(sections_.back().handle, *next));
  }
}

std::optional<ResumeValue> StoryRunner::follow(const SectionStep& step) {
  DWE_ASSERT(!sections_.empty());
  return std::visit(
      [&](const auto& outcome) -> std::optional<ResumeValue> {
        using Outcome = std::decay_t<decltype(outcome)>;
        if constexpr (std::is_same_v<Outcome, Wait>) {
          return follow(outcome);
        } else if constexpr (std::is_same_v<Outcome, SectionEnded>) {
          endScript();
          return std::nullopt;
        } else if constexpr (std::is_same_v<Outcome, SectionReturned>) {
          return returnFromTop();
        } else {
          fail(messages::formatMessage(messages::SectionFailedLog, sections_.back().ref.script,
                                       sections_.back().ref.section, outcome.message));
          return std::nullopt;
        }
      },
      step);
}

std::optional<ResumeValue> StoryRunner::follow(const Wait& wait) {
  if (const auto* jump = std::get_if<FileJump>(&wait)) {
    return jumpTo(*jump);
  }
  const auto& frames = std::get<FrameWait>(wait);
  framesToWait_ = std::max<std::int64_t>(frames.frames, 1);
  waitResult_ = frames.result;
  return std::nullopt;
}

std::optional<ResumeValue> StoryRunner::jumpTo(const FileJump& jump) {
  if (jump.linkDepth == 0) {
    // A tail jump: nothing is left to return to, so the target takes the jumping section's place.
    host_->close(sections_.back().handle);
    sections_.pop_back();
  } else {
    const SectionRef from = sections_.back().ref;
    const std::uint64_t depth = heldLinkDepth() + jump.linkDepth;
    if (depth > MaxLinkDepth) {
      fail(messages::formatMessage(messages::TooDeep, from.script, from.section, depth,
                                   MaxLinkDepth));
      return std::nullopt;
    }
    sections_.back().linkDepth = jump.linkDepth;
  }
  auto handle = host_->start(jump.target);
  if (!handle) {
    fail(messages::formatMessage(messages::SectionMissing, jump.target.script, jump.target.section,
                                 handle.error().message));
    return std::nullopt;
  }
  sections_.push_back({.ref = jump.target, .handle = *handle});
  return ResumeValue{};
}

std::uint64_t StoryRunner::heldLinkDepth() const {
  return std::accumulate(
      sections_.begin(), sections_.end(), std::uint64_t{0},
      [](std::uint64_t depth, const RunningSection& section) { return depth + section.linkDepth; });
}

std::optional<ResumeValue> StoryRunner::returnFromTop() {
  const RunningSection top = sections_.back();
  host_->close(top.handle);
  sections_.pop_back();
  if (sections_.empty()) {
    // A jump_return with an empty stack reads outside the original's script stack (script_interp.c:
    // scriptInstruction10to27), so it is an error rather than a rule
    // (docs/architecture/interprete-historia.md, Q11).
    fail(messages::formatMessage(messages::ReturnWithNothing, top.ref.script, top.ref.section));
    return std::nullopt;
  }
  // The section that jumped gets nil back from its jump_to_file and the subroutine it jumped from
  // returns; it is counted again when it next jumps.
  sections_.back().linkDepth = 0;
  return ResumeValue{};
}

void StoryRunner::fail(std::string_view message) {
  core::logError("{}", message);
  endScript();
}

void StoryRunner::endScript() {
  for (const RunningSection& section : sections_ | std::views::reverse) {
    host_->close(section.handle);
  }
  sections_.clear();
  framesToWait_ = 0;
  waitResult_.reset();
}

} // namespace dwe::game::story
