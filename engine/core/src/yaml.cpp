#include <dwe/core/assert.hpp>
#include <dwe/core/yaml.hpp>

#include <yaml.h>

#include <algorithm>
#include <climits>
#include <format>
#include <optional>
#include <unordered_set>
#include <utility>

namespace dwe::core {

YamlNode::YamlNode(Kind kind) : kind_(kind) {}

YamlNode YamlNode::scalar(std::string text, ScalarStyle style) {
    YamlNode node{Kind::Scalar};
    node.text_ = std::move(text);
    node.style_ = style;
    return node;
}

YamlNode YamlNode::sequence() {
    return YamlNode{Kind::Sequence};
}

YamlNode YamlNode::mapping() {
    return YamlNode{Kind::Mapping};
}

const std::string& YamlNode::text() const {
    DWE_ASSERT(isScalar());
    return text_;
}

std::span<const YamlNode> YamlNode::items() const {
    DWE_ASSERT(isSequence());
    return items_;
}

std::span<const YamlEntry> YamlNode::entries() const {
    DWE_ASSERT(isMapping());
    return entries_;
}

const YamlNode* YamlNode::find(std::string_view key) const {
    DWE_ASSERT(isMapping());
    const auto entry = std::ranges::find(entries_, key, &YamlEntry::key);
    return entry == entries_.end() ? nullptr : &entry->value;
}

void YamlNode::append(YamlNode item) {
    DWE_ASSERT(isSequence());
    items_.push_back(std::move(item));
}

void YamlNode::set(std::string key, YamlNode value) {
    DWE_ASSERT(isMapping());
    const auto entry = std::ranges::find(entries_, key, &YamlEntry::key);
    if (entry != entries_.end()) {
        entry->value = std::move(value);
    } else {
        entries_.push_back(YamlEntry{.key = std::move(key), .value = std::move(value)});
    }
}

bool operator==(const YamlNode& lhs, const YamlNode& rhs) {
    if (lhs.kind_ != rhs.kind_) {
        return false;
    }
    switch (lhs.kind_) {
    case YamlNode::Kind::Scalar:
        return lhs.text_ == rhs.text_;
    case YamlNode::Kind::Sequence:
        return lhs.items_ == rhs.items_;
    case YamlNode::Kind::Mapping:
        return std::ranges::equal(lhs.entries_, rhs.entries_, [](const YamlEntry& a, const YamlEntry& b) {
            return a.key == b.key && a.value == b.value;
        });
    }
    return false;
}

// Builds the tree from libyaml's events, checking what the document may contain.
class YamlTreeBuilder {
public:
    explicit YamlTreeBuilder(const YamlLimits& limits) : limits_(limits) {}

    // Takes one event; returns false at the end of the stream.
    Result<bool> handle(const yaml_event_t& event) {
        switch (event.type) {
        case YAML_STREAM_START_EVENT:
        case YAML_DOCUMENT_END_EVENT:
        case YAML_NO_EVENT:
            return true;
        case YAML_STREAM_END_EVENT:
            return false;
        case YAML_DOCUMENT_START_EVENT:
            if (sawDocument_) {
                return failAt(event.start_mark, "a file holds a single document");
            }
            sawDocument_ = true;
            return true;
        case YAML_ALIAS_EVENT:
            return failAt(event.start_mark, "anchors and aliases are not supported");
        case YAML_SCALAR_EVENT: {
            if (auto checked = checkProperties(event, event.data.scalar.anchor, event.data.scalar.tag); !checked) {
                return std::unexpected{checked.error()};
            }
            const std::string_view text{reinterpret_cast<const char*>(event.data.scalar.value),
                                        event.data.scalar.length};
            const auto style = event.data.scalar.style == YAML_PLAIN_SCALAR_STYLE ? YamlNode::ScalarStyle::Plain
                                                                                  : YamlNode::ScalarStyle::Quoted;
            return add(placed(YamlNode::scalar(std::string{text}, style), event.start_mark));
        }
        case YAML_SEQUENCE_START_EVENT:
            if (auto checked = checkProperties(event, event.data.sequence_start.anchor, event.data.sequence_start.tag);
                !checked) {
                return std::unexpected{checked.error()};
            }
            return open(placed(YamlNode::sequence(), event.start_mark));
        case YAML_MAPPING_START_EVENT:
            if (auto checked = checkProperties(event, event.data.mapping_start.anchor, event.data.mapping_start.tag);
                !checked) {
                return std::unexpected{checked.error()};
            }
            return open(placed(YamlNode::mapping(), event.start_mark));
        case YAML_SEQUENCE_END_EVENT:
        case YAML_MAPPING_END_EVENT: {
            YamlNode node = std::move(open_.back().node);
            open_.pop_back();
            return add(std::move(node));
        }
        }
        return true;
    }

    // The finished tree; an empty document is an empty scalar.
    YamlNode take() { return root_ ? std::move(*root_) : YamlNode::scalar(""); }

private:
    struct OpenNode {
        YamlNode node;
        std::optional<std::string> key;
        // Kept apart from the node so checking for duplicates stays linear on hostile input.
        std::unordered_set<std::string> keys;
    };

    static Result<bool> failAt(const yaml_mark_t& mark, std::string_view problem) {
        return fail(std::format("line {}, column {}: {}", mark.line + 1, mark.column + 1, problem));
    }

    static YamlNode placed(YamlNode node, const yaml_mark_t& mark) {
        node.line_ = mark.line + 1;
        node.column_ = mark.column + 1;
        return node;
    }

    static Result<bool> checkProperties(const yaml_event_t& event, const yaml_char_t* anchor, const yaml_char_t* tag) {
        if (anchor != nullptr) {
            return failAt(event.start_mark, "anchors and aliases are not supported");
        }
        if (tag != nullptr) {
            return failAt(event.start_mark, "tags are not supported");
        }
        return true;
    }

    Result<bool> open(YamlNode node) {
        if (open_.size() >= limits_.maxDepth) {
            return fail(std::format("line {}, column {}: nesting deeper than {} levels", node.line(), node.column(),
                                    limits_.maxDepth));
        }
        open_.push_back(OpenNode{.node = std::move(node), .key = std::nullopt, .keys = {}});
        return true;
    }

    Result<bool> add(YamlNode node) {
        if (open_.empty()) {
            root_ = std::move(node);
            return true;
        }
        OpenNode& parent = open_.back();
        if (parent.node.isSequence()) {
            parent.node.items_.push_back(std::move(node));
        } else if (parent.key) {
            parent.node.entries_.push_back(YamlEntry{.key = std::move(*parent.key), .value = std::move(node)});
            parent.key.reset();
        } else {
            if (!node.isScalar()) {
                return fail(
                    std::format("line {}, column {}: mapping keys must be scalars", node.line(), node.column()));
            }
            if (!parent.keys.insert(node.text_).second) {
                return fail(
                    std::format("line {}, column {}: duplicate key '{}'", node.line(), node.column(), node.text_));
            }
            parent.key = std::move(node.text_);
        }
        return true;
    }

    YamlLimits limits_;
    std::vector<OpenNode> open_;
    std::optional<YamlNode> root_;
    bool sawDocument_ = false;
};

namespace {

class Parser {
public:
    Parser() : ready_(yaml_parser_initialize(&parser_) != 0) {}
    ~Parser() { yaml_parser_delete(&parser_); }
    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&) = delete;
    Parser(Parser&&) = delete;
    Parser& operator=(Parser&&) = delete;

    [[nodiscard]] bool ready() const { return ready_; }
    yaml_parser_t* get() { return &parser_; }

private:
    yaml_parser_t parser_{};
    bool ready_;
};

class Event {
public:
    Event() = default;
    ~Event() { yaml_event_delete(&event_); }
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    Event(Event&&) = delete;
    Event& operator=(Event&&) = delete;

    yaml_event_t* get() { return &event_; }

private:
    yaml_event_t event_{};
};

std::unexpected<Error> parserError(const yaml_parser_t& parser) {
    const std::string_view problem = parser.problem != nullptr ? parser.problem : "invalid YAML";
    if (parser.error == YAML_READER_ERROR) {
        return fail(std::format("byte {}: {}", parser.problem_offset, problem));
    }
    std::string message =
        std::format("line {}, column {}: {}", parser.problem_mark.line + 1, parser.problem_mark.column + 1, problem);
    if (parser.context != nullptr) {
        message += std::format(" ({} at line {}, column {})", parser.context, parser.context_mark.line + 1,
                               parser.context_mark.column + 1);
    }
    return fail(std::move(message));
}

class Emitter {
public:
    Emitter() : ready_(yaml_emitter_initialize(&emitter_) != 0) {
        if (ready_) {
            yaml_emitter_set_output(&emitter_, &Emitter::write, &output_);
            yaml_emitter_set_encoding(&emitter_, YAML_UTF8_ENCODING);
            yaml_emitter_set_unicode(&emitter_, 1);
            yaml_emitter_set_break(&emitter_, YAML_LN_BREAK);
            yaml_emitter_set_indent(&emitter_, 2);
            // No folding of long scalars: a line break would depend on the width setting.
            yaml_emitter_set_width(&emitter_, -1);
        }
    }
    ~Emitter() { yaml_emitter_delete(&emitter_); }
    Emitter(const Emitter&) = delete;
    Emitter& operator=(const Emitter&) = delete;
    Emitter(Emitter&&) = delete;
    Emitter& operator=(Emitter&&) = delete;

    [[nodiscard]] bool ready() const { return ready_; }

    // Emits an initialized event; libyaml takes ownership of it.
    bool emit(yaml_event_t& event) { return yaml_emitter_emit(&emitter_, &event) != 0; }

    bool emitNode(const YamlNode& node) {
        yaml_event_t event{};
        switch (node.kind()) {
        case YamlNode::Kind::Scalar:
            return emitScalar(node.text(), node.style());
        case YamlNode::Kind::Sequence:
            if (yaml_sequence_start_event_initialize(&event, nullptr, nullptr, 1, YAML_BLOCK_SEQUENCE_STYLE) == 0 ||
                !emit(event)) {
                return false;
            }
            for (const YamlNode& item : node.items()) {
                if (!emitNode(item)) {
                    return false;
                }
            }
            return yaml_sequence_end_event_initialize(&event) != 0 && emit(event);
        case YamlNode::Kind::Mapping:
            if (yaml_mapping_start_event_initialize(&event, nullptr, nullptr, 1, YAML_BLOCK_MAPPING_STYLE) == 0 ||
                !emit(event)) {
                return false;
            }
            for (const YamlEntry& entry : node.entries()) {
                if (!emitScalar(entry.key, YamlNode::ScalarStyle::Plain) || !emitNode(entry.value)) {
                    return false;
                }
            }
            return yaml_mapping_end_event_initialize(&event) != 0 && emit(event);
        }
        return false;
    }

    std::string& output() { return output_; }

private:
    static int write(void* data, unsigned char* buffer, std::size_t size) {
        static_cast<std::string*>(data)->append(reinterpret_cast<const char*>(buffer), size);
        return 1;
    }

    bool emitScalar(const std::string& text, YamlNode::ScalarStyle style) {
        if (text.size() > INT_MAX) {
            return false;
        }
        // Plain scalars let libyaml quote when it must; implicit tags keep it from writing !!str.
        const yaml_scalar_style_t yamlStyle =
            style == YamlNode::ScalarStyle::Plain ? YAML_ANY_SCALAR_STYLE : YAML_DOUBLE_QUOTED_SCALAR_STYLE;
        yaml_event_t event{};
        return yaml_scalar_event_initialize(&event, nullptr, nullptr, reinterpret_cast<const yaml_char_t*>(text.data()),
                                            static_cast<int>(text.size()), 1, 1, yamlStyle) != 0 &&
               emit(event);
    }

    yaml_emitter_t emitter_{};
    std::string output_;
    bool ready_;
};

} // namespace

Result<YamlNode> parseYaml(std::string_view text, const YamlLimits& limits) {
    if (text.size() > limits.maxBytes) {
        return fail(std::format("the text is {} bytes, more than the limit of {}", text.size(), limits.maxBytes));
    }
    Parser parser;
    if (!parser.ready()) {
        return fail("could not start the YAML parser");
    }
    yaml_parser_set_input_string(parser.get(), reinterpret_cast<const unsigned char*>(text.data()), text.size());
    yaml_parser_set_encoding(parser.get(), YAML_UTF8_ENCODING);

    YamlTreeBuilder builder{limits};
    while (true) {
        Event event;
        if (yaml_parser_parse(parser.get(), event.get()) == 0) {
            return parserError(*parser.get());
        }
        const Result<bool> more = builder.handle(*event.get());
        if (!more) {
            return std::unexpected{more.error()};
        }
        if (!*more) {
            return builder.take();
        }
    }
}

Result<std::string> emitYaml(const YamlNode& node) {
    Emitter emitter;
    if (!emitter.ready()) {
        return fail("could not start the YAML emitter");
    }
    yaml_event_t event{};
    const bool emitted = yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING) != 0 && emitter.emit(event) &&
                         yaml_document_start_event_initialize(&event, nullptr, nullptr, nullptr, 1) != 0 &&
                         emitter.emit(event) && emitter.emitNode(node) &&
                         yaml_document_end_event_initialize(&event, 1) != 0 && emitter.emit(event) &&
                         yaml_stream_end_event_initialize(&event) != 0 && emitter.emit(event);
    if (!emitted) {
        return fail("could not write the YAML: a text is not valid UTF-8 or is too long");
    }
    return std::move(emitter.output());
}

} // namespace dwe::core
