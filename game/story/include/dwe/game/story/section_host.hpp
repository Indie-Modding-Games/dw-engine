#pragma once

#include <dwe/core/error.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

// How the runner runs a section without knowing Lua: the session implements SectionHost with the script VM, one
// coroutine per running section, and the tests with fakes (docs/architecture/interprete-historia.md, "Dónde vive").
namespace dwe::game::story {

// A section of a script: the script id (0 for the map-head script) and the section id.
struct SectionRef {
    std::uint16_t script = 0;
    std::uint16_t section = 0;

    friend bool operator==(const SectionRef&, const SectionRef&) = default;
};

// The value a section gets back from the order that waited: nil, or an integer.
using ResumeValue = std::optional<std::int64_t>;

// The section waits for frames of the original: it resumes once `frames` frames have begun, at least one, and gets
// `result` back.
struct FrameWait {
    std::int64_t frames = 1;
    ResumeValue result;
};

// The section jumps to a section of another script (jump_to_file), which runs in the same frame. The original only
// moves the script pointer (script_interp.c: scriptInstruction10to27): outside any subroutine (linkDepth 0) the
// section that jumped is done and the target takes its place. Inside linkDepth jump_and_link calls, their return
// frames stay on the original's stack, so the section that jumped waits under the target and, when the target
// returns with nothing to return to, gets nil back from its jump and its subroutine returns to its caller. The
// host counts linkDepth: the subroutine calls the jump is made from.
struct FileJump {
    SectionRef target;
    std::uint32_t linkDepth = 0;
};

// What a section waits for when it yields.
using Wait = std::variant<FrameWait, FileJump>;

// The section ended the script: end_section, end_script, a map change, or a routine that gave back ENDED.
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

    // Gets a section ready to run, without running it. Fails when the script or the section does not exist.
    virtual core::Result<SectionHandle> start(SectionRef section) = 0;
    // Runs a section until it waits or leaves: from its start the first time, otherwise from the order that waited,
    // which gets `value` back.
    virtual SectionStep resume(SectionHandle section, ResumeValue value) = 0;
    // Drops a section, whether it finished or not.
    virtual void close(SectionHandle section) = 0;
};

} // namespace dwe::game::story
