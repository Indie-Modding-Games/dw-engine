#include "schema_pattern.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>

namespace dwe::core {

namespace {

constexpr std::size_t Unbounded = std::numeric_limits<std::size_t>::max();
// Bounds that keep a hostile pattern from taking much memory or time: the program grows with every repetition.
constexpr std::size_t MaxPatternLength = 1024;
constexpr std::size_t MaxRepeat = 1000;
constexpr std::size_t MaxProgramSize = 20000;
constexpr std::size_t MaxGroupDepth = 32;
constexpr std::size_t AsciiSize = 128;

} // namespace

// The tree a pattern is read into before it is compiled.
struct PatternNode {
    enum class Kind : std::uint8_t { Set, Start, End, Sequence, Alternation, Repeat };

    Kind kind = Kind::Sequence;
    std::bitset<AsciiSize> set;
    std::vector<PatternNode> children;
    std::size_t min = 0;
    std::size_t max = 0;
};

namespace {

PatternNode nodeOf(PatternNode::Kind kind) {
    PatternNode node;
    node.kind = kind;
    return node;
}

bool isDecimalDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isMetacharacter(char c) {
    return std::string_view{"\\^$.|?*+()[]{}"}.contains(c);
}

bool isPrintableAscii(char c) {
    return c >= ' ' && c <= '~';
}

bool isPunctuation(char c) {
    return isPrintableAscii(c) && c != ' ' && !isDecimalDigit(c) && (c < 'a' || c > 'z') && (c < 'A' || c > 'Z');
}

// Reads the subset of ECMAScript patterns that JsonSchema documents into a tree.
class RegexParser {
public:
    explicit RegexParser(std::string_view source) : source_(source) {}

    Result<PatternNode> parse() {
        auto node = parseAlternation(0);
        if (node && position_ < source_.size()) {
            return failHere("unexpected ')'");
        }
        return node;
    }

private:
    [[nodiscard]] std::unexpected<Error> failHere(std::string_view problem) const {
        return fail(std::format("pattern '{}': {} at character {}", source_, problem, position_ + 1));
    }

    [[nodiscard]] bool atEnd() const { return position_ >= source_.size(); }
    [[nodiscard]] char peek() const { return source_[position_]; }

    Result<PatternNode> parseAlternation(std::size_t depth) {
        PatternNode alternation = nodeOf(PatternNode::Kind::Alternation);
        while (true) {
            auto sequence = parseSequence(depth);
            if (!sequence) {
                return sequence;
            }
            alternation.children.push_back(std::move(*sequence));
            if (atEnd() || peek() != '|') {
                break;
            }
            ++position_;
        }
        if (alternation.children.size() == 1) {
            return std::move(alternation.children.front());
        }
        return alternation;
    }

    Result<PatternNode> parseSequence(std::size_t depth) {
        PatternNode sequence = nodeOf(PatternNode::Kind::Sequence);
        while (!atEnd() && peek() != '|' && peek() != ')') {
            auto atom = parseAtom(depth);
            if (!atom) {
                return atom;
            }
            if (auto repeated = parseQuantifier(std::move(*atom)); repeated) {
                sequence.children.push_back(std::move(*repeated));
            } else {
                return repeated;
            }
        }
        return sequence;
    }

    Result<PatternNode> parseAtom(std::size_t depth) {
        const char c = peek();
        ++position_;
        switch (c) {
        case '^':
            return nodeOf(PatternNode::Kind::Start);
        case '$':
            return nodeOf(PatternNode::Kind::End);
        case '(': {
            if (depth + 1 > MaxGroupDepth) {
                return failHere("too many nested groups");
            }
            if (!atEnd() && peek() == '?') {
                return failHere("'(?' groups are not supported");
            }
            auto group = parseAlternation(depth + 1);
            if (!group) {
                return group;
            }
            if (atEnd() || peek() != ')') {
                return failHere("missing ')'");
            }
            ++position_;
            return group;
        }
        case '[':
            return parseClass();
        case '\\': {
            auto escaped = parseEscape();
            if (!escaped) {
                return std::unexpected{escaped.error()};
            }
            return single(*escaped);
        }
        default:
            break;
        }
        if (isMetacharacter(c)) {
            --position_;
            return failHere(std::format("'{}' is not supported here", c));
        }
        if (!isPrintableAscii(c)) {
            --position_;
            return failHere("only printable ASCII characters are supported");
        }
        return single(c);
    }

    static PatternNode single(char c) {
        PatternNode node = nodeOf(PatternNode::Kind::Set);
        node.set.set(static_cast<unsigned char>(c));
        return node;
    }

    // After a backslash: only punctuation, which stands for itself. Classes such as \d are not supported.
    Result<char> parseEscape() {
        if (atEnd()) {
            return failHere("the pattern ends with '\\'");
        }
        const char c = peek();
        if (!isPunctuation(c)) {
            return failHere(std::format("'\\{}' is not supported; only punctuation can be escaped", c));
        }
        ++position_;
        return c;
    }

    Result<PatternNode> parseClass() {
        PatternNode node = nodeOf(PatternNode::Kind::Set);
        if (!atEnd() && peek() == '^') {
            return failHere("negated classes are not supported");
        }
        while (!atEnd() && peek() != ']') {
            auto first = classCharacter();
            if (!first) {
                return std::unexpected{first.error()};
            }
            char last = *first;
            if (position_ + 1 < source_.size() && peek() == '-' && source_[position_ + 1] != ']') {
                ++position_;
                auto end = classCharacter();
                if (!end) {
                    return std::unexpected{end.error()};
                }
                if (*end < *first) {
                    return failHere("a range ends before it starts");
                }
                last = *end;
            }
            for (char c = *first; c <= last; ++c) {
                node.set.set(static_cast<unsigned char>(c));
                if (c == last) {
                    break;
                }
            }
        }
        if (atEnd()) {
            return failHere("missing ']'");
        }
        ++position_;
        if (node.set.none()) {
            return failHere("an empty class matches nothing");
        }
        return node;
    }

    Result<char> classCharacter() {
        const char c = peek();
        if (c == '\\') {
            ++position_;
            return parseEscape();
        }
        if (!isPrintableAscii(c)) {
            return failHere("only printable ASCII characters are supported");
        }
        ++position_;
        return c;
    }

    Result<PatternNode> parseQuantifier(PatternNode atom) {
        if (atEnd()) {
            return atom;
        }
        std::size_t min = 0;
        std::size_t max = 0;
        switch (peek()) {
        case '?':
            max = 1;
            ++position_;
            break;
        case '*':
            max = Unbounded;
            ++position_;
            break;
        case '+':
            min = 1;
            max = Unbounded;
            ++position_;
            break;
        case '{': {
            auto bounds = parseBounds();
            if (!bounds) {
                return std::unexpected{bounds.error()};
            }
            min = bounds->first;
            max = bounds->second;
            break;
        }
        default:
            return atom;
        }
        if (atom.kind == PatternNode::Kind::Start || atom.kind == PatternNode::Kind::End) {
            return failHere("an anchor cannot be repeated");
        }
        if (!atEnd() && (peek() == '?' || peek() == '*' || peek() == '+' || peek() == '{')) {
            return failHere("lazy and stacked quantifiers are not supported");
        }
        PatternNode repeat = nodeOf(PatternNode::Kind::Repeat);
        repeat.min = min;
        repeat.max = max;
        repeat.children.push_back(std::move(atom));
        return repeat;
    }

    // Reads {n}, {n,} or {n,m}.
    Result<std::pair<std::size_t, std::size_t>> parseBounds() {
        ++position_;
        const auto number = [this]() -> std::optional<std::size_t> {
            const std::size_t start = position_;
            while (!atEnd() && isDecimalDigit(peek())) {
                ++position_;
            }
            std::size_t value = 0;
            const auto [end, error] = std::from_chars(source_.data() + start, source_.data() + position_, value);
            if (start == position_ || error != std::errc{} || value > MaxRepeat) {
                return std::nullopt;
            }
            return value;
        };
        const auto min = number();
        if (!min) {
            return failHere(std::format("a repetition count must be a number up to {}", MaxRepeat));
        }
        std::size_t max = *min;
        if (!atEnd() && peek() == ',') {
            ++position_;
            max = Unbounded;
            if (!atEnd() && peek() != '}') {
                const auto upper = number();
                if (!upper || *upper < *min) {
                    return failHere(std::format("a repetition count must be a number from {} to {}", *min, MaxRepeat));
                }
                max = *upper;
            }
        }
        if (atEnd() || peek() != '}') {
            return failHere("missing '}'");
        }
        ++position_;
        return std::pair{*min, max};
    }

    std::string_view source_;
    std::size_t position_ = 0;
};

} // namespace

Result<SchemaPattern> SchemaPattern::compile(std::string_view source) {
    if (source.size() > MaxPatternLength) {
        return fail(std::format("a pattern may have up to {} characters", MaxPatternLength));
    }
    auto tree = RegexParser{source}.parse();
    if (!tree) {
        return std::unexpected{tree.error()};
    }
    SchemaPattern pattern;
    pattern.source_ = source;
    if (!pattern.emit(*tree)) {
        return fail(std::format("pattern '{}' is too large", source));
    }
    pattern.push(Op::Match);
    return pattern;
}

bool SchemaPattern::search(std::string_view text) const {
    constexpr std::size_t Unmarked = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> marks(program_.size(), Unmarked);
    std::vector<std::size_t> current;
    std::vector<std::size_t> next;
    std::vector<std::size_t> pending;
    // Adds the threads that start at pc, following jumps; true when one of them matches.
    const auto add = [&](std::vector<std::size_t>& threads, std::size_t pc, std::size_t position) {
        pending.assign(1, pc);
        while (!pending.empty()) {
            const std::size_t at = pending.back();
            pending.pop_back();
            if (marks[at] == position) {
                continue;
            }
            marks[at] = position;
            const Instruction& instruction = program_[at];
            switch (instruction.op) {
            case Op::Byte:
                threads.push_back(at);
                break;
            case Op::Match:
                return true;
            case Op::Jump:
                pending.push_back(instruction.x);
                break;
            case Op::Split:
                pending.push_back(instruction.y);
                pending.push_back(instruction.x);
                break;
            case Op::Start:
                if (position == 0) {
                    pending.push_back(at + 1);
                }
                break;
            case Op::End:
                if (position == text.size()) {
                    pending.push_back(at + 1);
                }
                break;
            }
        }
        return false;
    };
    for (std::size_t position = 0;; ++position) {
        if (add(current, 0, position)) {
            return true;
        }
        if (position == text.size()) {
            return false;
        }
        const auto byte = static_cast<unsigned char>(text[position]);
        next.clear();
        for (const std::size_t pc : current) {
            if (byte < AsciiSize && program_[pc].set.test(byte) && add(next, pc + 1, position + 1)) {
                return true;
            }
        }
        std::swap(current, next);
    }
}

std::size_t SchemaPattern::push(Op op) {
    program_.emplace_back().op = op;
    return program_.size() - 1;
}

// Appends the code of a node; false when the program grows past its limit.
bool SchemaPattern::emit(const PatternNode& node) {
    if (program_.size() > MaxProgramSize) {
        return false;
    }
    switch (node.kind) {
    case PatternNode::Kind::Set:
        program_[push(Op::Byte)].set = node.set;
        return true;
    case PatternNode::Kind::Start:
        push(Op::Start);
        return true;
    case PatternNode::Kind::End:
        push(Op::End);
        return true;
    case PatternNode::Kind::Sequence:
        return std::ranges::all_of(node.children, [this](const PatternNode& child) { return emit(child); });
    case PatternNode::Kind::Alternation:
        return emitAlternation(node.children);
    case PatternNode::Kind::Repeat:
        return emitRepeat(node.children.front(), node.min, node.max);
    }
    return false;
}

bool SchemaPattern::emitAlternation(std::span<const PatternNode> choices) {
    std::vector<std::size_t> jumps;
    for (std::size_t i = 0; i + 1 < choices.size(); ++i) {
        const std::size_t split = push(Op::Split);
        program_[split].x = split + 1;
        if (!emit(choices[i])) {
            return false;
        }
        jumps.push_back(push(Op::Jump));
        program_[split].y = program_.size();
    }
    if (!emit(choices.back())) {
        return false;
    }
    for (const std::size_t jump : jumps) {
        program_[jump].x = program_.size();
    }
    return true;
}

bool SchemaPattern::emitRepeat(const PatternNode& child, std::size_t min, std::size_t max) {
    for (std::size_t i = 0; i < min; ++i) {
        if (!emit(child)) {
            return false;
        }
    }
    if (max == Unbounded) {
        const std::size_t loop = push(Op::Split);
        program_[loop].x = loop + 1;
        if (!emit(child)) {
            return false;
        }
        program_[push(Op::Jump)].x = loop;
        program_[loop].y = program_.size();
        return true;
    }
    for (std::size_t i = min; i < max; ++i) {
        const std::size_t split = push(Op::Split);
        program_[split].x = split + 1;
        if (!emit(child)) {
            return false;
        }
        program_[split].y = program_.size();
    }
    return true;
}

} // namespace dwe::core
