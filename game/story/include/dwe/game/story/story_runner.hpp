#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/story/frame_systems.hpp>
#include <dwe/game/story/section_host.hpp>
#include <dwe/game/story/story_ports.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

// The one interpreter of the story (docs/architecture/interprete-historia.md, "El runtime"): it
// runs one script at a time, a frame of the original every few engine ticks, and resumes the
// running section once a frame unless it waits, as script_interp.c: tickScript does.
namespace dwe::game::story {

// When a requested section runs its first order.
enum class StartTiming : std::uint8_t {
  // At the next frame of the original: the story runs before the field, so a section the field asks
  // for begins in
  // the frame after (main.c: gameLoop).
  NextFrame,
  // At once, inside the request (script_interp.c: runMapHeadScript).
  Now,
};

// What became of a request.
enum class RequestOutcome : std::uint8_t {
  Accepted,
  // Another script was running or about to start: the original drops the request (tamer.c:
  // checkMapInteraction).
  Dropped,
};

// What started a script. The original marks the map's chain and talking as events
// (script_interp.c: callScriptSection); a cell, the toilet and the systems' sections are not. An
// event's map change shows no map name unless a script asks (consumeMapChangeShowName).
enum class SectionSource : std::uint8_t {
  Plain,
  Event,
};

// The game rules of the script flow, from the ruleset's story section.
struct StoryFlowRules {
  // While a script holds the player still the runner keeps frozenValue in frozenVariable, and it
  // puts back the variable's value from the start of the script when the script ends or changes
  // the map (script_interp.c: scriptPauseGame, tickScript).
  std::int64_t frozenVariable = 0;
  std::int64_t frozenValue = 0;
  // Every freeze clears this trigger, which keeps a talk's two sides from turning to each other.
  std::int64_t talkFacingTrigger = 0;
  // Whether a script's map change shows the new map's name: the variable says by event (no name
  // for an event), always or never, and goes back to by event; any other value keeps the last
  // choice (script_interp.c: consumeMapChangeShowName).
  std::int64_t showNameVariable = 0;
  std::int64_t showNameByEvent = 0;
  std::int64_t showNameAlways = 0;
  std::int64_t showNameNever = 0;
};

// What the script flow changes outside the runner. Without a port the runner leaves that part
// alone, as the tests of the bare flow need.
struct StoryFlowPorts {
  StoryStatePort* state = nullptr;
  FieldControlPort* control = nullptr;
  MapChangePort* mapChange = nullptr;
  // What ticks inside the story's frame, in this order (script_interp.c: tickScript).
  // NOLINTNEXTLINE(readability-redundant-member-init): the default lets designated initializers leave it out.
  std::vector<ScriptFrameSystem*> frameSystems = {};
};

// A system that keeps time with the script, as script_interp.c: tickScript ticks the textboxes
// before it runs the script's next order and closes them when the script stops.
class ScriptFrameListener {
public:
  ScriptFrameListener() = default;
  ScriptFrameListener(const ScriptFrameListener&) = delete;
  ScriptFrameListener& operator=(const ScriptFrameListener&) = delete;
  ScriptFrameListener(ScriptFrameListener&&) = delete;
  ScriptFrameListener& operator=(ScriptFrameListener&&) = delete;
  virtual ~ScriptFrameListener() = default;

  // A script starts (script_interp.c: callScriptSection).
  virtual void scriptStarted() = 0;
  // A frame of the original begins while a script runs, before its section goes on.
  virtual void frameBegan() = 0;
  // The script ended or left for a map change (script_interp.c: tickScript, closeAllTextboxes).
  virtual void scriptStopped() = 0;
};

class StoryRunner {
public:
  // The script of the map-head sections, and the section of a map's script that runs when the
  // map is set up (script_interp.c: getScript, returnFromScriptFile).
  static constexpr std::uint16_t MapHeadScript = 0;
  static constexpr std::uint16_t MapEntrySection = 0xFE;

  // Guards against runaway scripts, not rules of the original: the engine's limits, far above what
  // the disc needs. How deep in subroutine calls, counted over the sections waiting under jumps, a
  // jump to another script may be. The original's script stack holds 8 frames (script_interp.c:
  // pushScriptStack); that is a limit of its memory, not of the game.
  static constexpr std::uint64_t MaxLinkDepth = 256;
  // How many times one frame may resume sections. The original has no limit: this only stops a
  // section that never waits from hanging the game.
  static constexpr std::uint32_t MaxResumesPerFrame = 1024;

  // ticksPerFrame: engine ticks per frame of the original, from the ruleset.
  static core::Result<StoryRunner> create(SectionHost& host, std::uint32_t ticksPerFrame,
                                          StoryFlowPorts ports = {}, StoryFlowRules rules = {});

  // Asks for a section to run.
  RequestOutcome request(SectionRef section, StartTiming timing,
                         SectionSource source = SectionSource::Plain);
  // Asks for a section of the map's script (set_script), as the field does for an NPC
  // (tamer.c: checkMapInteraction, callScriptSection(CURRENT_SCRIPT_ID, ...)). Dropped while the
  // map has no script.
  RequestOutcome requestMapSection(std::uint16_t section, StartTiming timing,
                                   SectionSource source = SectionSource::Plain);
  // One engine tick: runs a frame of the original every ticksPerFrame ticks.
  void tick();

  // The player is on a new map: walked there, loaded a save or ended a script's map change. Runs
  // the map's map-head section at once, over whatever ran (map.c: changeMap); after a script's map
  // change the script goes on in the same frame (map.c: scriptTickChangeMap).
  void enterMap(std::int32_t map);
  // The field did not play the script's map change (the story lock keeps the map closed): the
  // arrival section the change left belongs to the other map's script and is dropped, so that
  // enterMap with the map the player is on ends the script there.
  void dropMapChangeArrival();

  // set_script: the map's script is `script`; once the section ends, the script's map-entry section
  // runs. The map was loaded by whoever entered it.
  core::Result<void> setScript(std::int64_t script, std::int64_t map);
  // section_on_exit: once the script ends, `section` of the map's script runs the next frame
  // (NoArrivalSection: none).
  core::Result<void> sectionOnExit(std::int64_t section);
  // Holds the player still for the script (script_interp.c: scriptPauseGame).
  void freezeWorld();
  // Whether set_script set up `map`'s script: until then the map's cells and NPCs have no
  // sections to run.
  [[nodiscard]] bool scriptsMap(std::int32_t map) const {
    return mapScript_.has_value() && scriptMap_ == map;
  }
  // The map's script set_script set up (ACTIVE_MAP_SCRIPT), if any.
  [[nodiscard]] std::optional<std::uint16_t> mapScript() const { return mapScript_; }

  // Tells `listener`, which must outlive the runner or be removed first, how the script runs.
  void addFrameListener(ScriptFrameListener& listener);
  void removeFrameListener(const ScriptFrameListener& listener);

  // The value the frozen variable goes back to when the script ends: routine 25 sets it
  // (script_interp.c: scriptInstruction64to7E, MAIN_D_80134FE7).
  void setVariableAtStart(std::int64_t value);

  // Whether a script is running or about to start.
  [[nodiscard]] bool busy() const {
    return pending_.has_value() || !sections_.empty() || continueAt_.has_value() || changingMap_;
  }
  // The sections now running, the first one requested first.
  [[nodiscard]] std::vector<SectionRef> runningSections() const;

private:
  struct RunningSection {
    SectionRef ref;
    SectionHandle handle = 0;
    // The subroutine calls it waits in, under a section it jumped to.
    std::uint32_t linkDepth = 0;
  };
  struct PendingSection {
    SectionRef ref;
    SectionSource source = SectionSource::Plain;
  };
  // What runs once the current section ends, as the original's script stack keeps it besides
  // subroutine returns (script_interp.c: returnFromScriptFile): the map's entry section after
  // set_script, or a section of the map's script after a map change or section_on_exit.
  struct MapLoaded {};
  struct RunSection {
    // Nothing: the map-head script's end, which goes on to the next continuation.
    std::optional<std::uint16_t> section;
  };
  using Continuation = std::variant<MapLoaded, RunSection>;

  StoryRunner(SectionHost& host, std::uint32_t ticksPerFrame, StoryFlowPorts ports,
              StoryFlowRules rules)
      : host_{&host}, ticksPerFrame_{ticksPerFrame}, ports_{std::move(ports)}, rules_{rules} {}

  void runFrame();
  void beginScript(SectionRef section, SectionSource source);
  void startSection(SectionRef section);
  bool openSection(SectionRef section);
  void run(ResumeValue value);
  // Handles how the top section left a resume; the value to resume with when the script goes on
  // now.
  std::optional<ResumeValue> follow(const SectionStep& step);
  std::optional<ResumeValue> follow(const Wait& wait);
  std::optional<ResumeValue> jumpTo(const FileJump& jump);
  void changeMap(const MapChangeWait& change);
  [[nodiscard]] std::uint64_t heldLinkDepth() const;
  std::optional<ResumeValue> returnFromTop();
  std::optional<ResumeValue> fail(std::string_view message);
  // The end of a section: runs the continuations, or ends the script when none is left. The value
  // to resume with when a section it opened goes on now.
  std::optional<ResumeValue> returnFromScript();
  bool openMapEntrySection();
  void continueWith(const RunSection& next);
  void finishScript();
  void faceTalkers();
  void closeSections();
  void stopScript();
  std::optional<ResumeValue> waitOn(WaitSource& source);
  bool chooseShowName();
  [[nodiscard]] std::int64_t variable(std::int64_t variable) const;
  // The state is the port's, not the runner's.
  void setVariable(std::int64_t variable, std::int64_t value) const;

  SectionHost* host_;
  std::uint32_t ticksPerFrame_;
  StoryFlowPorts ports_;
  StoryFlowRules rules_;
  std::uint32_t ticksIntoFrame_ = 0;
  std::optional<PendingSection> pending_;
  std::vector<RunningSection> sections_;
  std::int64_t framesToWait_ = 0;
  ResumeValue waitResult_;
  // The system the running section waits on, if any.
  WaitSource* waitSource_ = nullptr;
  std::vector<ScriptFrameListener*> frameListeners_;
  // A wait for the field (wait_for_entity), checked once a frame.
  std::function<bool()> waitUntil_;
  // The section the script started at: the talker's script id for a talk (SCRIPT_SECTION).
  std::uint16_t sectionAtStart_ = 0;
  // The script stack's continuations. The original never clears them between scripts, so one
  // left by a script that ended another way runs at the next script's end.
  std::vector<Continuation> continuations_;
  std::optional<RunSection> continueAt_;
  // The current map's script (set_script) and the map entered last.
  std::optional<std::uint16_t> mapScript_;
  std::optional<std::int32_t> enteredMap_;
  // The map set_script named last.
  std::optional<std::int32_t> scriptMap_;
  bool changingMap_ = false;
  bool frozen_ = false;
  SectionSource source_ = SectionSource::Plain;
  std::int64_t variableAtStart_ = 0;
  bool showName_ = false;
};

} // namespace dwe::game::story
