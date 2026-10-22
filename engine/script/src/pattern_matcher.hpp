#pragma once

#include <lua.hpp>

#include <array>
#include <cstddef>
#include <string_view>

namespace dwe::script::detail {

// Lua's string patterns (lstrlib.c: match and its helpers), with every step charged to the running
// call's instruction budget: Lua's own matcher can backtrack for hours inside one C call, where the
// instruction hook never runs. Same results and errors as Lua 5.5's.
//
// Malformed patterns and an exhausted budget raise Lua errors, which unwind with longjmp: the
// matcher is trivially destructible and works on the subject and pattern of Lua strings, which end
// in '\0' like Lua's matcher expects.
class PatternMatcher {
public:
  // The most captures one pattern may have (LUA_MAXCAPTURES in lstrlib.c).
  static constexpr int MaxCaptures = 32;

  // The subject and the pattern, after any '^' anchor, must be Lua strings that outlive the
  // matcher.
  PatternMatcher(lua_State* state, std::string_view subject, std::string_view pattern);

  // Sets the state that errors are raised in: gmatch's iterator may run in another coroutine than
  // gmatch.
  void setState(lua_State* state) { state_ = state; }

  // Matches the whole pattern at a position of the subject: the end of the match, or nullptr.
  // Clears the captures of the previous attempt first.
  const char* matchAt(const char* position);

  // Pushes the captures of the last match, or the whole match [start, end) when the pattern has
  // none and start is not null. Returns how many values it pushed.
  int pushCaptures(const char* start, const char* end);

  // Pushes the capture at index, or the whole match when the pattern has no captures and index is
  // 0.
  void pushCapture(int index, const char* start, const char* end);

  // Adds the capture at index to a buffer, as pushCapture would give it.
  void addCapture(luaL_Buffer& buffer, int index, const char* start, const char* end);

  [[nodiscard]] const char* subjectBegin() const { return subjectBegin_; }
  [[nodiscard]] const char* subjectEnd() const { return subjectEnd_; }
  [[nodiscard]] const char* patternBegin() const { return patternBegin_; }

private:
  // Where a pattern item leaves the match: go on with the rest of the pattern, or a final result.
  struct Step {
    const char* subject;
    const char* pattern;
    bool finished;
  };

  struct Capture {
    const char* start = nullptr;
    std::ptrdiff_t length = 0;
  };

  static Step proceed(const char* subject, const char* pattern) {
    return {.subject = subject, .pattern = pattern, .finished = false};
  }
  static Step finish(const char* result) {
    return {.subject = result, .pattern = nullptr, .finished = true};
  }

  const char* match(const char* subject, const char* pattern);
  Step matchItem(const char* subject, const char* pattern);
  Step matchEscape(const char* subject, const char* pattern);
  Step matchFrontier(const char* subject, const char* set);
  Step matchClass(const char* subject, const char* pattern);
  Step matchRepetition(const char* subject, const char* pattern, const char* classEnd);

  const char* classEnd(const char* pattern);
  bool singleMatch(const char* subject, const char* pattern, const char* classEnd) const;
  const char* matchBalance(const char* subject, const char* pattern);
  const char* maxExpand(const char* subject, const char* pattern, const char* classEnd);
  const char* minExpand(const char* subject, const char* pattern, const char* classEnd);
  const char* startCapture(const char* subject, const char* pattern, std::ptrdiff_t what);
  const char* endCapture(const char* subject, const char* pattern);
  const char* matchCapture(const char* subject, char index);
  int captureIndex(char index);
  int captureToClose();

  // The text of a capture, or nullptr after pushing a position capture.
  const char* captureText(int index, const char* start, const char* end, std::size_t& length);

  void chargeSteps(std::size_t steps);

  lua_State* state_;
  const char* subjectBegin_;
  const char* subjectEnd_;
  const char* patternBegin_;
  const char* patternEnd_;
  int depthLeft_ = 0;
  int level_ = 0;
  std::array<Capture, MaxCaptures> captures_{};
};

} // namespace dwe::script::detail
