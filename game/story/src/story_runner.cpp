#include <dwe/core/assert.hpp>
#include <dwe/core/log.hpp>
#include <dwe/game/story/story_runner.hpp>

#include <algorithm>
#include <limits>
#include <numeric>
#include <ranges>
#include <type_traits>
#include <utility>
#include <variant>

#include "messages.hpp"

namespace dwe::game::story {

namespace {

constexpr std::int64_t LastSectionNumber = std::numeric_limits<std::uint16_t>::max();

bool isSectionNumber(std::int64_t number) {
  return number >= 0 && number <= LastSectionNumber;
}

} // namespace

core::Result<StoryRunner> StoryRunner::create(SectionHost& host, std::uint32_t ticksPerFrame,
                                              StoryFlowPorts ports, StoryFlowRules rules) {
  if (ticksPerFrame == 0) {
    return core::fail(std::string{messages::NoTicksPerFrame});
  }
  return StoryRunner{host, ticksPerFrame, ports, rules};
}

RequestOutcome StoryRunner::request(SectionRef section, StartTiming timing, SectionSource source) {
  if (busy()) {
    std::uint16_t running = mapScript_.value_or(section.script);
    if (pending_) {
      running = pending_->ref.script;
    } else if (!sections_.empty()) {
      running = sections_.front().ref.script;
    }
    core::logDebug("{}", messages::formatMessage(messages::RequestDropped, section.section,
                                                 section.script, running));
    return RequestOutcome::Dropped;
  }
  if (timing == StartTiming::Now) {
    beginScript(section, source);
  } else {
    pending_ = PendingSection{.ref = section, .source = source};
  }
  return RequestOutcome::Accepted;
}

RequestOutcome StoryRunner::requestMapSection(std::uint16_t section, StartTiming timing,
                                              SectionSource source) {
  if (!mapScript_) {
    core::logDebug("[port:story] callScriptSection (script_interp.c:2244): section {} of the map's "
                   "script, but no set_script ran on this map; dropped",
                   section);
    return RequestOutcome::Dropped;
  }
  return request({.script = *mapScript_, .section = section}, timing, source);
}

void StoryRunner::tick() {
  ++ticksIntoFrame_;
  if (ticksIntoFrame_ < ticksPerFrame_) {
    return;
  }
  ticksIntoFrame_ = 0;
  runFrame();
}

void StoryRunner::enterMap(std::int32_t map) {
  enteredMap_ = map;
  const bool scripted = std::exchange(changingMap_, false);
  core::logDebug("{}", messages::formatMessage(messages::PortEnterMap, map, scripted));
  if (!isSectionNumber(map)) {
    core::logError("{}", messages::formatMessage(messages::NoMapHeadSection, map));
    if (scripted) {
      finishScript();
    }
    return;
  }
  // map.c: changeMap starts the map-head section over whatever runs; the continuations stay.
  closeSections();
  pending_.reset();
  continueAt_.reset();
  beginScript({.script = MapHeadScript, .section = static_cast<std::uint16_t>(map)},
              SectionSource::Event);
  if (!scripted) {
    return;
  }
  // map.c: scriptTickChangeMap holds the player again once the map is in, and tickScript goes on
  // in the same frame with the arrival section the chain left. Freezing only while a script runs
  // is an engine guard against holding the player with no script to let go.
  frozen_ = false;
  core::logDebug(
      "{}", messages::formatMessage(messages::PortMapChangeDone, busy(), continueAt_.has_value()));
  if (!busy()) {
    // map.c: scriptTickChangeMap gives the player back when the chain ended the script.
    if (ports_.control != nullptr) {
      ports_.control->release();
    }
    return;
  }
  freezeWorld();
  if (const std::optional<RunSection> next = std::exchange(continueAt_, std::nullopt)) {
    continueWith(*next);
  }
}

core::Result<void> StoryRunner::setScript(std::int64_t script, std::int64_t map) {
  if (!isSectionNumber(script)) {
    return core::fail(messages::formatMessage(messages::NoSuchScript, script));
  }
  mapScript_ = static_cast<std::uint16_t>(script);
  scriptMap_ = static_cast<std::int32_t>(map);
  core::logDebug("{}", messages::formatMessage(messages::PortSetScript, script, map));
  if (enteredMap_ != map) {
    // The original loads the map here (script_interp.c: scriptInstructionFBtoFF); the engine
    // entered it before the chain ran, and on the disc set_script always names that map.
    core::logError("{}",
                   messages::formatMessage(messages::MapNotEntered, map, enteredMap_.value_or(-1)));
  }
  continuations_.emplace_back(MapLoaded{});
  return {};
}

core::Result<void> StoryRunner::sectionOnExit(std::int64_t section) {
  if (section != NoArrivalSection && !isSectionNumber(section)) {
    return core::fail(messages::formatMessage(messages::NoSuchSection, section));
  }
  core::logDebug("{}", messages::formatMessage(messages::PortSectionOnExit, section));
  RunSection next;
  if (section != NoArrivalSection) {
    next.section = static_cast<std::uint16_t>(section);
  }
  continuations_.emplace_back(next);
  setVariable(rules_.frozenVariable, variableAtStart_);
  return {};
}

void StoryRunner::freezeWorld() {
  core::logDebug("{}",
                 messages::formatMessage(messages::PortFreeze, !frozen_, rules_.talkFacingTrigger));
  if (!frozen_) {
    frozen_ = true;
    if (ports_.control != nullptr) {
      ports_.control->freeze();
    }
    setVariable(rules_.frozenVariable, rules_.frozenValue);
    faceTalkers();
  }
  if (ports_.state != nullptr) {
    if (auto cleared = ports_.state->setTrigger(rules_.talkFacingTrigger, false); !cleared) {
      core::logError("{}",
                     messages::formatMessage(messages::FlowStateFailed, cleared.error().message));
    }
  }
}

void StoryRunner::addFrameListener(ScriptFrameListener& listener) {
  frameListeners_.push_back(&listener);
}

void StoryRunner::removeFrameListener(const ScriptFrameListener& listener) {
  std::erase(frameListeners_, &listener);
}

std::vector<SectionRef> StoryRunner::runningSections() const {
  return sections_ | std::views::transform(&RunningSection::ref) | std::ranges::to<std::vector>();
}

void StoryRunner::runFrame() {
  if (const std::optional<PendingSection> pending = std::exchange(pending_, std::nullopt)) {
    beginScript(pending->ref, pending->source);
    return;
  }
  // script_interp.c: tickScript ticks the boxes and the scripted moves before anything else, also
  // during a map change.
  if (busy()) {
    for (ScriptFrameSystem* system : ports_.frameSystems) {
      system->scriptFrame();
    }
  }
  if (changingMap_) {
    return;
  }
  // A continuation runs at the start of the frame after the section that left it.
  if (const std::optional<RunSection> next = std::exchange(continueAt_, std::nullopt)) {
    continueWith(*next);
    return;
  }
  if (sections_.empty()) {
    return;
  }
  for (ScriptFrameListener* listener : frameListeners_) {
    listener->frameBegan();
  }
  if (waitSource_ != nullptr) {
    // script_interp.c: tickScript runs the next order once the system cleared the active one.
    const std::optional<ResumeValue> done = waitSource_->poll();
    if (!done) {
      return;
    }
    waitSource_ = nullptr;
    run(*done);
    return;
  }
  // script_interp.c: tickScript counts the wait down and, when it reaches 0, runs the next order in
  // the same frame.
  if (waitUntil_) {
    if (!waitUntil_()) {
      return;
    }
    waitUntil_ = nullptr;
  } else {
    --framesToWait_;
    if (framesToWait_ > 0) {
      return;
    }
  }
  run(std::exchange(waitResult_, std::nullopt));
}

void StoryRunner::beginScript(SectionRef section, SectionSource source) {
  // script_interp.c: callScriptSection.
  source_ = source;
  sectionAtStart_ = section.section;
  for (ScriptFrameSystem* system : ports_.frameSystems) {
    system->scriptBegins();
  }
  variableAtStart_ = variable(rules_.frozenVariable);
  frozen_ = false;
  core::logDebug("{}",
                 messages::formatMessage(messages::PortBeginScript, section.script, section.section,
                                         source == SectionSource::Event, variableAtStart_));
  for (ScriptFrameListener* listener : frameListeners_) {
    listener->scriptStarted();
  }
  startSection(section);
}

void StoryRunner::startSection(SectionRef section) {
  if (openSection(section)) {
    run(std::nullopt);
  }
}

bool StoryRunner::openSection(SectionRef section) {
  auto handle = host_->start(section);
  if (!handle) {
    core::logError("{}", messages::formatMessage(messages::SectionMissing, section.script,
                                                 section.section, handle.error().message));
    return false;
  }
  sections_.push_back({.ref = section, .handle = *handle});
  return true;
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
          return returnFromScript();
        } else if constexpr (std::is_same_v<Outcome, SectionReturned>) {
          return returnFromTop();
        } else {
          return fail(messages::formatMessage(messages::SectionFailedLog,
                                              sections_.back().ref.script,
                                              sections_.back().ref.section, outcome.message));
        }
      },
      step);
}

std::optional<ResumeValue> StoryRunner::follow(const Wait& wait) {
  if (const auto* jump = std::get_if<FileJump>(&wait)) {
    return jumpTo(*jump);
  }
  if (const auto* change = std::get_if<MapChangeWait>(&wait)) {
    changeMap(*change);
    return std::nullopt;
  }
  if (const auto* source = std::get_if<SourceWait>(&wait)) {
    DWE_ASSERT(source->source != nullptr);
    return waitOn(*source->source);
  }
  if (const auto* until = std::get_if<UntilWait>(&wait)) {
    framesToWait_ = 0;
    waitUntil_ = until->done;
    return std::nullopt;
  }
  const auto& frames = std::get<FrameWait>(wait);
  framesToWait_ = std::max<std::int64_t>(frames.frames, 1);
  waitResult_ = frames.result;
  return std::nullopt;
}

std::optional<ResumeValue> StoryRunner::waitOn(WaitSource& source) {
  // A system that is done at once lets the section go on in the same frame, as an order that never
  // set itself active does.
  if (std::optional<ResumeValue> done = source.poll()) {
    return done;
  }
  waitSource_ = &source;
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
      return fail(messages::formatMessage(messages::TooDeep, from.script, from.section, depth,
                                          MaxLinkDepth));
    }
    sections_.back().linkDepth = jump.linkDepth;
  }
  auto handle = host_->start(jump.target);
  if (!handle) {
    return fail(messages::formatMessage(messages::SectionMissing, jump.target.script,
                                        jump.target.section, handle.error().message));
  }
  sections_.push_back({.ref = jump.target, .handle = *handle});
  return ResumeValue{};
}

void StoryRunner::changeMap(const MapChangeWait& change) {
  // script_interp.c: scriptInstruction46to58 freezes, picks the name and leaves the arrival on the
  // stack; tickScript then drops the section, puts the frozen variable back and starts the change.
  freezeWorld();
  const bool showName = chooseShowName();
  core::logDebug("{}",
                 messages::formatMessage(messages::PortMapChangeWait, change.map, change.exit,
                                         change.arrival.value_or(NoArrivalSection), showName));
  continuations_.emplace_back(RunSection{.section = change.arrival});
  closeSections();
  stopScript();
  setVariable(rules_.frozenVariable, variableAtStart_);
  changingMap_ = true;
  if (ports_.mapChange != nullptr) {
    ports_.mapChange->startMapChange(change.map, change.exit, showName);
  }
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
    return fail(
        messages::formatMessage(messages::ReturnWithNothing, top.ref.script, top.ref.section));
  }
  // The section that jumped gets nil back from its jump_to_file and the subroutine it jumped from
  // returns; it is counted again when it next jumps.
  sections_.back().linkDepth = 0;
  return ResumeValue{};
}

std::optional<ResumeValue> StoryRunner::fail(std::string_view message) {
  // The original cannot fail; a failed section ends as end_section does, so the chain goes on.
  core::logError("{}", message);
  core::logDebug("{}", messages::formatMessage(messages::PortSectionFailed, continuations_.size()));
  return returnFromScript();
}

std::optional<ResumeValue> StoryRunner::returnFromScript() {
  // script_interp.c: returnFromScriptFile.
  closeSections();
  while (!continuations_.empty()) {
    const Continuation next = continuations_.back();
    continuations_.pop_back();
    if (const auto* section = std::get_if<RunSection>(&next)) {
      core::logDebug("{}", messages::formatMessage(messages::PortPopRunSection,
                                                   section->section.value_or(NoArrivalSection)));
      continueAt_ = *section;
      return std::nullopt;
    }
    const bool opened = openMapEntrySection();
    core::logDebug(
        "{}", messages::formatMessage(messages::PortPopMapLoaded, mapScript_.value_or(0), opened));
    if (opened) {
      return ResumeValue{};
    }
  }
  finishScript();
  return std::nullopt;
}

bool StoryRunner::openMapEntrySection() {
  if (!mapScript_) {
    core::logError("{}", std::string{messages::NoMapScript});
    return false;
  }
  const SectionRef entry{.script = *mapScript_, .section = MapEntrySection};
  auto handle = host_->start(entry);
  if (!handle) {
    // Most maps have no entry section (script_interp.c: getScriptSection gives none).
    core::logDebug("{}", messages::formatMessage(messages::NoEntrySection, entry.script,
                                                 handle.error().message));
    return false;
  }
  sections_.push_back({.ref = entry, .handle = *handle});
  return true;
}

void StoryRunner::continueWith(const RunSection& next) {
  if (!next.section) {
    // script_interp.c: setMapHeadActive points at the map-head script's end, which returns again.
    core::logDebug("{}", std::string{messages::PortMapHeadEnd});
    if (const std::optional<ResumeValue> value = returnFromScript()) {
      run(*value);
    }
    return;
  }
  if (!mapScript_) {
    core::logError("{}", std::string{messages::NoMapScript});
  } else if (openSection({.script = *mapScript_, .section = *next.section})) {
    run(std::nullopt);
    return;
  }
  if (const std::optional<ResumeValue> value = returnFromScript()) {
    run(*value);
  }
}

void StoryRunner::finishScript() {
  // script_interp.c: tickScript, once the script is over.
  core::logDebug("{}", messages::formatMessage(messages::PortFinish, rules_.frozenVariable,
                                               variableAtStart_, frozen_));
  stopScript();
  setVariable(rules_.frozenVariable, variableAtStart_);
  if (std::exchange(frozen_, false) && ports_.control != nullptr) {
    ports_.control->release();
  }
}

void StoryRunner::closeSections() {
  for (const RunningSection& section : sections_ | std::views::reverse) {
    host_->close(section.handle);
  }
  sections_.clear();
  framesToWait_ = 0;
  waitResult_.reset();
  waitSource_ = nullptr;
  waitUntil_ = nullptr;
}

void StoryRunner::stopScript() {
  for (ScriptFrameListener* listener : frameListeners_) {
    listener->scriptStopped();
  }
}

void StoryRunner::faceTalkers() {
  // script_interp.c: scriptPauseGame turns the two sides of a talk to each other unless the talk
  // trigger says not to; the caller clears that trigger afterwards.
  if (source_ != SectionSource::Event || ports_.state == nullptr) {
    return;
  }
  const auto noFacing = ports_.state->trigger(rules_.talkFacingTrigger);
  if (!noFacing || *noFacing) {
    return;
  }
  for (ScriptFrameSystem* system : ports_.frameSystems) {
    system->talkFrozen(sectionAtStart_);
  }
}

bool StoryRunner::chooseShowName() {
  const std::int64_t choice = variable(rules_.showNameVariable);
  if (choice == rules_.showNameByEvent) {
    showName_ = source_ != SectionSource::Event;
  } else if (choice == rules_.showNameAlways) {
    showName_ = true;
  } else if (choice == rules_.showNameNever) {
    showName_ = false;
  }
  setVariable(rules_.showNameVariable, rules_.showNameByEvent);
  core::logDebug("{}", messages::formatMessage(messages::PortShowName, choice,
                                               source_ == SectionSource::Event, showName_));
  return showName_;
}

std::int64_t StoryRunner::variable(std::int64_t variable) const {
  if (ports_.state == nullptr) {
    return 0;
  }
  auto value = ports_.state->variable(variable);
  if (!value) {
    core::logError("{}", messages::formatMessage(messages::FlowStateFailed, value.error().message));
    return 0;
  }
  return *value;
}

void StoryRunner::setVariable(std::int64_t variable, std::int64_t value) const {
  if (ports_.state == nullptr) {
    return;
  }
  if (auto written = ports_.state->setVariable(variable, value); !written) {
    core::logError("{}",
                   messages::formatMessage(messages::FlowStateFailed, written.error().message));
  }
}

} // namespace dwe::game::story
