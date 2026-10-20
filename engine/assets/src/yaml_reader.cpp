#include <dwe/assets/yaml_reader.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <system_error>

namespace dwe::assets {

namespace {

using core::YamlNode;

const YamlNode& emptyNode() {
    static const YamlNode empty = YamlNode::scalar("");
    return empty;
}

bool contains(std::initializer_list<std::string_view> keys, std::string_view key) {
    return std::ranges::find(keys, key) != keys.end();
}

} // namespace

bool YamlReader::expectKeys(const YamlNode& node, std::string_view what, std::initializer_list<std::string_view> keys) {
    return expectKeys(node, what, keys, {});
}

bool YamlReader::expectKeys(const YamlNode& node, std::string_view what,
                            std::initializer_list<std::string_view> required,
                            std::initializer_list<std::string_view> optional) {
    if (!node.isMapping()) {
        fail(node, std::format("{} must be a mapping", what));
        return false;
    }
    for (const core::YamlEntry& entry : node.entries()) {
        if (!contains(required, entry.key) && !contains(optional, entry.key)) {
            fail(entry.value, std::format("unknown key '{}' in {}", entry.key, what));
            return false;
        }
    }
    for (const std::string_view key : required) {
        if (node.find(key) == nullptr) {
            fail(node, std::format("{} has no '{}'", what, key));
            return false;
        }
    }
    return true;
}

const YamlNode& YamlReader::field(const YamlNode& mapping, std::string_view key) {
    const YamlNode* value = mapping.isMapping() ? mapping.find(key) : nullptr;
    if (value == nullptr) {
        fail(mapping, std::format("missing '{}'", key));
        return emptyNode();
    }
    return *value;
}

std::span<const YamlNode> YamlReader::sequence(const YamlNode& node, std::string_view what) {
    if (!node.isSequence()) {
        fail(node, std::format("{} must be a list", what));
        return {};
    }
    return node.items();
}

std::span<const YamlNode> YamlReader::sequence(const YamlNode& node, std::string_view what, std::size_t count) {
    const std::span<const YamlNode> items = sequence(node, what);
    if (ok() && items.size() != count) {
        fail(node, std::format("{} must have {} items, not {}", what, count, items.size()));
        return {};
    }
    return items;
}

std::optional<std::int64_t> YamlReader::readInteger(const YamlNode& node) {
    if (!ok()) {
        return std::nullopt;
    }
    if (!node.isScalar()) {
        fail(node, "expected a number");
        return std::nullopt;
    }
    const std::string& text = node.text();
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || text.empty()) {
        fail(node, std::format("'{}' is not a whole number", text));
        return std::nullopt;
    }
    return value;
}

float YamlReader::number(const YamlNode& node) {
    if (!ok()) {
        return 0.0F;
    }
    if (!node.isScalar()) {
        fail(node, "expected a number");
        return 0.0F;
    }
    const std::string& text = node.text();
    float value = 0.0F;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || text.empty() || !std::isfinite(value)) {
        fail(node, std::format("'{}' is not a number", text));
        return 0.0F;
    }
    return value;
}

bool YamlReader::boolean(const YamlNode& node) {
    if (!ok()) {
        return false;
    }
    if (node.isScalar() && node.style() == YamlNode::ScalarStyle::Plain) {
        if (node.text() == "true") {
            return true;
        }
        if (node.text() == "false") {
            return false;
        }
    }
    fail(node, "expected true or false");
    return false;
}

std::string YamlReader::text(const YamlNode& node) {
    if (!ok()) {
        return {};
    }
    if (!node.isScalar()) {
        fail(node, "expected a single value");
        return {};
    }
    return node.text();
}

std::optional<std::string> YamlReader::optionalText(const YamlNode& node) {
    if (isNull(node)) {
        return std::nullopt;
    }
    return text(node);
}

bool YamlReader::isNull(const YamlNode& node) {
    return node.isScalar() && node.style() == YamlNode::ScalarStyle::Plain &&
           (node.text() == "null" || node.text() == "~");
}

void YamlReader::fail(const YamlNode& at, std::string_view problem) {
    if (!error_) {
        error_ = core::Error{std::format("line {}, column {}: {}", at.line(), at.column(), problem)};
    }
}

core::Error YamlReader::error(std::string_view context) const {
    if (!error_) {
        return core::Error{};
    }
    return core::Error{std::format("{}: {}", context, error_->message)};
}

} // namespace dwe::assets
