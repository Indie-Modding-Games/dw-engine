#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// How the runner runs a section without knowing Lua: the session implements SectionHost with the
// script VM, one coroutine per running section, and the tests with fakes
// (docs/architecture/interprete-historia.md, "Dónde vive").
namespace dwe::game::story {

// A section of a script: the script id (0 for the map-head script) and the section id.
struct SectionRef {
  std::uint16_t script = 0;
  std::uint16_t section = 0;

  friend bool operator==(const SectionRef&, const SectionRef&) = default;
};

// The value a section gets back from the order that waited: nil, or an integer.
using ResumeValue = std::optional<std::int64_t>;

// The section waits for frames of the original: it resumes once `frames` frames have begun, at
// least one, and gets `result` back.
struct FrameWait {
  std::int64_t frames = 1;
  ResumeValue result;
};

// The section jumps to a section of another script (jump_to_file), which runs in the same frame.
// The original only moves the script pointer (script_interp.c: scriptInstruction10to27): outside
// any subroutine (linkDepth 0) the section that jumped is done and the target takes its place.
// Inside linkDepth jump_and_link calls, their return frames stay on the original's stack, so the
// section that jumped waits under the target and, when the target returns with nothing to return
// to, gets nil back from its jump and its subroutine returns to its caller. The host counts
// linkDepth: the subroutine calls the jump is made from.
struct FileJump {
  SectionRef target;
  std::uint32_t linkDepth = 0;
};

// The section number warp_to and section_on_exit give for none (script_interp.c:
// returnFromScriptFile).
inline constexpr std::int64_t NoArrivalSection = 0xFF;

// The section changes the map (warp_to, teleport): it ends at once, the screen fades out and the
// map's script chain runs on the new map; then `arrival`, a section of the new map's script, runs
// (script_interp.c: scriptInstruction46to58, map.c: scriptTickChangeMap).
struct MapChangeWait {
  std::int32_t map = 0;
  std::int32_t exit = 0;
  // Nothing: no section runs on arrival, only what the map's chain leaves.
  std::optional<std::uint16_t> arrival;
  friend bool operator==(const MapChangeWait&, const MapChangeWait&) = default;
};

// A system a section waits on, such as a textbox: the runner asks it whether the section goes on
// when the order yields, and then once a frame of the original, as script_interp.c: tickScript
// leaves an order active until the system clears it.
class WaitSource {
public:
  WaitSource() = default;
  WaitSource(const WaitSource&) = delete;
  WaitSource& operator=(const WaitSource&) = delete;
  WaitSource(WaitSource&&) = delete;
  WaitSource& operator=(WaitSource&&) = delete;
  virtual ~WaitSource() = default;

  // Nothing while the section still waits; otherwise the value its order gives back.
  [[nodiscard]] virtual std::optional<ResumeValue> poll() = 0;
};

// The section waits until `source` lets it go on; the source must outlive the wait.
struct SourceWait {
  WaitSource* source = nullptr;

  friend bool operator==(const SourceWait&, const SourceWait&) = default;
};

// The section waits until `done` says so (wait_for_entity): checked once a frame after the frame
// systems ran; when it holds, the section goes on in that frame and gets nil back
// (script_interp.c: tickScript).
struct UntilWait {
  std::function<bool()> done;
};

// What a section waits for when it yields.
using Wait = std::variant<FrameWait, FileJump, MapChangeWait, SourceWait, UntilWait>;

// The section ended the script: end_section, end_script, a map change, or a routine that gave back
// ENDED.
struct SectionEnded {};

// The section returned nothing: a jump_return that left its function.
struct SectionReturned {};

// The section stopped with an error (a Lua error, or its budget ran out).
struct SectionFailed {
  std::string message;
};

// How a section left a resume.
using SectionStep = std::variant<Wait, SectionEnded, SectionReturned, SectionFailed>;

// A section the host has started, until it is closed.
using SectionHandle = std::uint32_t;

class SectionHost {
public:
  SectionHost() = default;
  SectionHost(const SectionHost&) = delete;
  SectionHost& operator=(const SectionHost&) = delete;
  SectionHost(SectionHost&&) = delete;
  SectionHost& operator=(SectionHost&&) = delete;
  virtual ~SectionHost() = default;

  // Gets a section ready to run, without running it. Fails when the script or the section does not
  // exist.
  virtual core::Result<SectionHandle> start(SectionRef section) = 0;
  // Runs a section until it waits or leaves: from its start the first time, otherwise from the
  // order that waited, which gets `value` back.
  virtual SectionStep resume(SectionHandle section, ResumeValue value) = 0;
  // Drops a section, whether it finished or not.
  virtual void close(SectionHandle section) = 0;
  // Where a section that waits stands in its code, for tools: the line of each function it is in,
  // the innermost first (the last is the section's own). Empty when the host cannot say.
  [[nodiscard]] virtual std::vector<std::int32_t> lines(SectionHandle /*section*/) const {
    return {};
  }
};

} // namespace dwe::game::story
