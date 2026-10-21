#include "pattern_matcher.hpp"

#include <cctype>
#include <cstring>
#include <utility>

#include "budget.hpp"
#include "lua_errors.hpp"
#include "messages.hpp"
#include "number_text.hpp"

namespace dwe::script::detail {
namespace {

// The deepest the matcher recurses, so a pattern cannot overflow the C stack (MAXCCALLS in lstrlib.c).
constexpr int MaxMatchDepth = 200;

// The length of a capture still open, and of a position capture `()`.
constexpr std::ptrdiff_t UnfinishedCapture = -1;
constexpr std::ptrdiff_t PositionCapture = -2;

constexpr char Escape = '%';

// Whether c is in the class that a letter after '%' names, such as %a or %D (match_class in lstrlib.c).
bool inClass(int c, int letter) {
    bool inside = false;
    switch (std::tolower(letter)) {
    case 'a':
        inside = std::isalpha(c) != 0;
        break;
    case 'c':
        inside = std::iscntrl(c) != 0;
        break;
    case 'd':
        inside = std::isdigit(c) != 0;
        break;
    case 'g':
        inside = std::isgraph(c) != 0;
        break;
    case 'l':
        inside = std::islower(c) != 0;
        break;
    case 'p':
        inside = std::ispunct(c) != 0;
        break;
    case 's':
        inside = std::isspace(c) != 0;
        break;
    case 'u':
        inside = std::isupper(c) != 0;
        break;
    case 'w':
        inside = std::isalnum(c) != 0;
        break;
    case 'x':
        inside = std::isxdigit(c) != 0;
        break;
    case 'z':
        inside = c == 0;
        break;
    default:
        return letter == c;
    }
    return std::islower(letter) != 0 ? inside : !inside;
}

int byteAt(const char* position) {
    return static_cast<unsigned char>(*position);
}

// Whether c is in the set [...] that starts at set and whose ']' is at setEnd (matchbracketclass in lstrlib.c).
bool inSet(int c, const char* set, const char* setEnd) {
    bool matches = true;
    if (*(set + 1) == '^') {
        matches = false;
        ++set;
    }
    while (++set < setEnd) {
        if (*set == Escape) {
            ++set;
            if (inClass(c, byteAt(set))) {
                return matches;
            }
        } else if (*(set + 1) == '-' && set + 2 < setEnd) {
            set += 2;
            if (byteAt(set - 2) <= c && c <= byteAt(set)) {
                return matches;
            }
        } else if (byteAt(set) == c) {
            return matches;
        }
    }
    return !matches;
}

} // namespace

PatternMatcher::PatternMatcher(lua_State* state, std::string_view subject, std::string_view pattern)
    : state_{state}, subjectBegin_{subject.data()}, subjectEnd_{subject.data() + subject.size()},
      patternBegin_{pattern.data()}, patternEnd_{pattern.data() + pattern.size()} {}

const char* PatternMatcher::matchAt(const char* position) {
    depthLeft_ = MaxMatchDepth;
    level_ = 0;
    chargeSteps(1);
    return match(position, patternBegin_);
}

void PatternMatcher::chargeSteps(std::size_t steps) {
    chargeBudget(state_, static_cast<std::int64_t>(steps));
}

// The pattern items one after another; the items that end the match (captures, repetitions) recurse.
const char* PatternMatcher::match(const char* subject, const char* pattern) {
    if (depthLeft_-- == 0) {
        raiseError(state_, {messages::PatternTooComplex});
    }
    Step step = proceed(subject, pattern);
    while (!step.finished && step.pattern != patternEnd_) {
        chargeSteps(1);
        step = matchItem(step.subject, step.pattern);
    }
    ++depthLeft_;
    return step.subject;
}

PatternMatcher::Step PatternMatcher::matchItem(const char* subject, const char* pattern) {
    switch (*pattern) {
    case '(':
        if (*(pattern + 1) == ')') {
            return finish(startCapture(subject, pattern + 2, PositionCapture));
        }
        return finish(startCapture(subject, pattern + 1, UnfinishedCapture));
    case ')':
        return finish(endCapture(subject, pattern + 1));
    case '$':
        if (pattern + 1 == patternEnd_) {
            return finish(subject == subjectEnd_ ? subject : nullptr);
        }
        return matchClass(subject, pattern);
    case Escape:
        return matchEscape(subject, pattern);
    default:
        return matchClass(subject, pattern);
    }
}

// %b, %f and back references; any other escape is a character class.
PatternMatcher::Step PatternMatcher::matchEscape(const char* subject, const char* pattern) {
    const char kind = *(pattern + 1);
    if (kind == 'b') {
        const char* end = matchBalance(subject, pattern + 2);
        return end != nullptr ? proceed(end, pattern + 4) : finish(nullptr);
    }
    if (kind == 'f') {
        return matchFrontier(subject, pattern + 2);
    }
    if (std::isdigit(static_cast<unsigned char>(kind)) != 0) {
        const char* end = matchCapture(subject, kind);
        return end != nullptr ? proceed(end, pattern + 2) : finish(nullptr);
    }
    return matchClass(subject, pattern);
}

PatternMatcher::Step PatternMatcher::matchFrontier(const char* subject, const char* set) {
    if (*set != '[') {
        raiseError(state_, {messages::FrontierWithoutSet});
    }
    const char* end = classEnd(set);
    const char previous = subject == subjectBegin_ ? '\0' : *(subject - 1);
    if (!inSet(static_cast<unsigned char>(previous), set, end - 1) && inSet(byteAt(subject), set, end - 1)) {
        return proceed(subject, end);
    }
    return finish(nullptr);
}

// A single character class with an optional repetition suffix.
PatternMatcher::Step PatternMatcher::matchClass(const char* subject, const char* pattern) {
    const char* end = classEnd(pattern);
    if (!singleMatch(subject, pattern, end)) {
        if (*end == '*' || *end == '?' || *end == '-') {
            return proceed(subject, end + 1);
        }
        return finish(nullptr);
    }
    return matchRepetition(subject, pattern, end);
}

PatternMatcher::Step PatternMatcher::matchRepetition(const char* subject, const char* pattern, const char* end) {
    switch (*end) {
    case '?': {
        const char* result = match(subject + 1, end + 1);
        return result != nullptr ? finish(result) : proceed(subject, end + 1);
    }
    case '+':
        return finish(maxExpand(subject + 1, pattern, end));
    case '*':
        return finish(maxExpand(subject, pattern, end));
    case '-':
        return finish(minExpand(subject, pattern, end));
    default:
        return proceed(subject + 1, end);
    }
}

const char* PatternMatcher::classEnd(const char* pattern) {
    const char first = *pattern++;
    if (first == Escape) {
        if (pattern == patternEnd_) {
            raiseError(state_, {messages::PatternEndsWithEscape});
        }
        return pattern + 1;
    }
    if (first != '[') {
        return pattern;
    }
    if (*pattern == '^') {
        ++pattern;
    }
    // The first character of a set may be ']' itself.
    do {
        if (pattern == patternEnd_) {
            raiseError(state_, {messages::PatternMissingBracket});
        }
        const bool escaped = *pattern == Escape;
        ++pattern;
        if (escaped && pattern < patternEnd_) {
            ++pattern;
        }
    } while (*pattern != ']');
    return pattern + 1;
}

bool PatternMatcher::singleMatch(const char* subject, const char* pattern, const char* end) const {
    if (subject >= subjectEnd_) {
        return false;
    }
    const int c = byteAt(subject);
    switch (*pattern) {
    case '.':
        return true;
    case Escape:
        return inClass(c, byteAt(pattern + 1));
    case '[':
        return inSet(c, pattern, end - 1);
    default:
        return byteAt(pattern) == c;
    }
}

const char* PatternMatcher::matchBalance(const char* subject, const char* pattern) {
    if (pattern >= patternEnd_ - 1) {
        raiseError(state_, {messages::PatternMissingBalanceArguments});
    }
    if (*subject != *pattern) {
        return nullptr;
    }
    const char open = *pattern;
    const char close = *(pattern + 1);
    int depth = 1;
    const char* position = subject;
    const char* result = nullptr;
    while (result == nullptr && ++position < subjectEnd_) {
        if (*position == close) {
            if (--depth == 0) {
                result = position + 1;
            }
        } else if (*position == open) {
            ++depth;
        }
    }
    chargeSteps(static_cast<std::size_t>(position - subject));
    return result;
}

// The longest run of the class, then shorter ones until the rest of the pattern matches.
const char* PatternMatcher::maxExpand(const char* subject, const char* pattern, const char* end) {
    std::ptrdiff_t count = 0;
    while (singleMatch(subject + count, pattern, end)) {
        ++count;
    }
    chargeSteps(static_cast<std::size_t>(count));
    for (; count >= 0; --count) {
        if (const char* result = match(subject + count, end + 1); result != nullptr) {
            return result;
        }
    }
    return nullptr;
}

// The shortest run of the class that lets the rest of the pattern match.
const char* PatternMatcher::minExpand(const char* subject, const char* pattern, const char* end) {
    while (true) {
        if (const char* result = match(subject, end + 1); result != nullptr) {
            return result;
        }
        if (!singleMatch(subject, pattern, end)) {
            return nullptr;
        }
        ++subject;
    }
}

const char* PatternMatcher::startCapture(const char* subject, const char* pattern, std::ptrdiff_t what) {
    if (level_ >= MaxCaptures) {
        raiseError(state_, {messages::TooManyCaptures});
    }
    captures_[static_cast<std::size_t>(level_)] = Capture{.start = subject, .length = what};
    ++level_;
    const char* result = match(subject, pattern);
    if (result == nullptr) {
        --level_;
    }
    return result;
}

const char* PatternMatcher::endCapture(const char* subject, const char* pattern) {
    Capture& capture = captures_[static_cast<std::size_t>(captureToClose())];
    capture.length = subject - capture.start;
    const char* result = match(subject, pattern);
    if (result == nullptr) {
        capture.length = UnfinishedCapture;
    }
    return result;
}

const char* PatternMatcher::matchCapture(const char* subject, char index) {
    const Capture& capture = captures_[static_cast<std::size_t>(captureIndex(index))];
    const auto length = static_cast<std::size_t>(capture.length);
    chargeSteps(1 + (length / BytesPerStep));
    if (std::cmp_greater_equal(subjectEnd_ - subject, length) && std::memcmp(capture.start, subject, length) == 0) {
        return subject + length;
    }
    return nullptr;
}

int PatternMatcher::captureIndex(char index) {
    const int level = index - '1';
    if (level < 0 || level >= level_ || captures_[static_cast<std::size_t>(level)].length == UnfinishedCapture) {
        const NumberText number{level + 1};
        raiseError(state_, {messages::InvalidCaptureIndex, number.text()});
    }
    return level;
}

int PatternMatcher::captureToClose() {
    for (int level = level_ - 1; level >= 0; --level) {
        if (captures_[static_cast<std::size_t>(level)].length == UnfinishedCapture) {
            return level;
        }
    }
    return raiseError(state_, {messages::InvalidPatternCapture});
}

const char* PatternMatcher::captureText(int index, const char* start, const char* end, std::size_t& length) {
    if (index >= level_) {
        if (index != 0) {
            const NumberText number{index + 1};
            raiseError(state_, {messages::InvalidCaptureIndex, number.text()});
        }
        length = static_cast<std::size_t>(end - start);
        return start;
    }
    const Capture& capture = captures_[static_cast<std::size_t>(index)];
    if (capture.length == UnfinishedCapture) {
        raiseError(state_, {messages::UnfinishedCapture});
    }
    if (capture.length == PositionCapture) {
        lua_pushinteger(state_, static_cast<lua_Integer>(capture.start - subjectBegin_) + 1);
        return nullptr;
    }
    length = static_cast<std::size_t>(capture.length);
    return capture.start;
}

void PatternMatcher::pushCapture(int index, const char* start, const char* end) {
    std::size_t length = 0;
    if (const char* text = captureText(index, start, end, length); text != nullptr) {
        lua_pushlstring(state_, text, length);
    }
}

void PatternMatcher::addCapture(luaL_Buffer& buffer, int index, const char* start, const char* end) {
    std::size_t length = 0;
    if (const char* text = captureText(index, start, end, length); text != nullptr) {
        luaL_addlstring(&buffer, text, length);
    } else {
        luaL_addvalue(&buffer);
    }
}

int PatternMatcher::pushCaptures(const char* start, const char* end) {
    const int count = level_ == 0 && start != nullptr ? 1 : level_;
    luaL_checkstack(state_, count, messages::TooManyCaptures);
    for (int index = 0; index < count; ++index) {
        pushCapture(index, start, end);
    }
    return count;
}

} // namespace dwe::script::detail
