#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/story/section_host.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// The one interpreter of the story (docs/architecture/interprete-historia.md, "El runtime"): it runs one script at a
// time, a frame of the original every few engine ticks, and resumes the running section once a frame unless it waits,
// as script_interp.c: tickScript does.
namespace dwe::game::story {

// When a requested section runs its first order.
enum class StartTiming : std::uint8_t {
    // At the next frame of the original: the story runs before the field, so a section the field asks for begins in
    // the frame after (main.c: gameLoop).
    NextFrame,
    // At once, inside the request (script_interp.c: runMapHeadScript).
    Now,
};

// What became of a request.
enum class RequestOutcome : std::uint8_t {
    Accepted,
    // Another script was running or about to start: the original drops the request (tamer.c: checkMapInteraction).
    Dropped,
};

class StoryRunner {
public:
    // Guards against runaway scripts, not rules of the original: the engine's limits, far above what the disc needs.
    // How deep in subroutine calls, counted over the sections waiting under jumps, a jump to another script may be.
    // The original's script stack holds 8 frames (script_interp.c: pushScriptStack); that is a limit of its memory,
    // not of the game.
    static constexpr std::uint64_t MaxLinkDepth = 256;
    // How many times one frame may resume sections. The original has no limit: this only stops a section that never
    // waits from hanging the game.
    static constexpr std::uint32_t MaxResumesPerFrame = 1024;

    // ticksPerFrame: engine ticks per frame of the original, from the ruleset.
    static core::Result<StoryRunner> create(SectionHost& host, std::uint32_t ticksPerFrame);

    // Asks for a section to run.
    RequestOutcome request(SectionRef section, StartTiming timing);
    // One engine tick: runs a frame of the original every ticksPerFrame ticks.
    void tick();

    // Whether a script is running or about to start.
    [[nodiscard]] bool busy() const { return pending_.has_value() || !sections_.empty(); }
    // The sections now running, the first one requested first.
    [[nodiscard]] std::vector<SectionRef> runningSections() const;

private:
    struct RunningSection {
        SectionRef ref;
        SectionHandle handle = 0;
        // The subroutine calls it waits in, under a section it jumped to.
        std::uint32_t linkDepth = 0;
    };

    StoryRunner(SectionHost& host, std::uint32_t ticksPerFrame) : host_{&host}, ticksPerFrame_{ticksPerFrame} {}

    void runFrame();
    void startSection(SectionRef section);
    void run(ResumeValue value);
    // Handles how the top section left a resume; the value to resume with when the script goes on now.
    std::optional<ResumeValue> follow(const SectionStep& step);
    std::optional<ResumeValue> follow(const Wait& wait);
    std::optional<ResumeValue> jumpTo(const FileJump& jump);
    [[nodiscard]] std::uint64_t heldLinkDepth() const;
    std::optional<ResumeValue> returnFromTop();
    void fail(std::string_view message);
    void endScript();

    SectionHost* host_;
    std::uint32_t ticksPerFrame_;
    std::uint32_t ticksIntoFrame_ = 0;
    std::optional<SectionRef> pending_;
    std::vector<RunningSection> sections_;
    std::int64_t framesToWait_ = 0;
    ResumeValue waitResult_;
};

} // namespace dwe::game::story
