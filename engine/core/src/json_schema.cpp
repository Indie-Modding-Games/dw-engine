#include <dwe/core/json_schema.hpp>
#include <dwe/core/messages.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <system_error>
#include <utility>

#include "schema_pattern.hpp"

namespace dwe::core {

namespace {

// --- Scalars ---

bool isDecimalDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isHexDigit(char c) {
    return isDecimalDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool isOctalDigit(char c) {
    return c >= '0' && c <= '7';
}

bool allOf(std::string_view text, bool (*predicate)(char)) {
    return !text.empty() && std::ranges::all_of(text, predicate);
}

std::string_view withoutSign(std::string_view text) {
    return text.starts_with('-') || text.starts_with('+') ? text.substr(1) : text;
}

bool isIntegerText(std::string_view text) {
    if (text.starts_with("0x")) {
        return allOf(text.substr(2), isHexDigit);
    }
    if (text.starts_with("0o")) {
        return allOf(text.substr(2), isOctalDigit);
    }
    return allOf(withoutSign(text), isDecimalDigit);
}

bool isInfinityText(std::string_view text) {
    const std::string_view body = withoutSign(text);
    return body == ".inf" || body == ".Inf" || body == ".INF";
}

bool isNanText(std::string_view text) {
    return text == ".nan" || text == ".NaN" || text == ".NAN";
}

// [-+]? ( . [0-9]+ | [0-9]+ ( . [0-9]* )? ) ( [eE] [-+]? [0-9]+ )?
bool isNumberText(std::string_view text) {
    if (isInfinityText(text) || isNanText(text)) {
        return true;
    }
    const std::string_view body = withoutSign(text);
    std::size_t position = 0;
    std::size_t digits = 0;
    const auto skipDigits = [&] {
        while (position < body.size() && isDecimalDigit(body[position])) {
            ++position;
            ++digits;
        }
    };
    skipDigits();
    if (position < body.size() && body[position] == '.') {
        ++position;
        skipDigits();
    }
    if (digits == 0) {
        return false;
    }
    if (position < body.size() && (body[position] == 'e' || body[position] == 'E')) {
        return allOf(withoutSign(body.substr(position + 1)), isDecimalDigit);
    }
    return position == body.size();
}

bool isNumeric(ScalarType type) {
    return type == ScalarType::Integer || type == ScalarType::Number;
}

// The value of an integer or number scalar. Integers past the range of a double lose precision, which no limit of
// a schema needs.
double numberValue(const YamlNode& scalar, ScalarType type) {
    const std::string& text = scalar.text();
    if (type == ScalarType::Integer && (text.starts_with("0x") || text.starts_with("0o"))) {
        std::uint64_t value = 0;
        const int base = text[1] == 'x' ? 16 : 8;
        const auto [end, error] = std::from_chars(text.data() + 2, text.data() + text.size(), value, base);
        return error == std::errc{} ? static_cast<double>(value) : std::numeric_limits<double>::infinity();
    }
    if (isNanText(text)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (isInfinityText(text)) {
        return text.starts_with('-') ? -std::numeric_limits<double>::infinity()
                                     : std::numeric_limits<double>::infinity();
    }
    // strtod reads every form the checks above let through, and saturates to infinity like a JSON reader would.
    return std::strtod(text.c_str(), nullptr);
}

bool isTrueText(std::string_view text) {
    return text == "true" || text == "True" || text == "TRUE";
}

std::string_view scalarTypeName(ScalarType type) {
    switch (type) {
    case ScalarType::Null:
        return "null";
    case ScalarType::Boolean:
        return "boolean";
    case ScalarType::Integer:
        return "integer";
    case ScalarType::Number:
        return "number";
    case ScalarType::String:
        return "string";
    }
    return "string";
}

std::string_view nodeTypeName(const YamlNode& node) {
    if (node.isMapping()) {
        return "object";
    }
    if (node.isSequence()) {
        return "array";
    }
    return scalarTypeName(scalarType(node));
}

// Compares two values as JSON does: numbers by value, other scalars by type and text, the rest by structure.
bool sameValue(const YamlNode& lhs, const YamlNode& rhs) {
    if (!lhs.isScalar() || !rhs.isScalar()) {
        return lhs == rhs;
    }
    const ScalarType lhsType = scalarType(lhs);
    const ScalarType rhsType = scalarType(rhs);
    if (isNumeric(lhsType) && isNumeric(rhsType)) {
        const double lhsValue = numberValue(lhs, lhsType);
        const double rhsValue = numberValue(rhs, rhsType);
        return !(lhsValue < rhsValue) && !(rhsValue < lhsValue);
    }
    if (lhsType != rhsType) {
        return false;
    }
    switch (lhsType) {
    case ScalarType::Null:
        return true;
    case ScalarType::Boolean:
        return isTrueText(lhs.text()) == isTrueText(rhs.text());
    default:
        return lhs.text() == rhs.text();
    }
}

// --- Types ---

using TypeMask = std::uint8_t;
constexpr TypeMask ObjectType = 1U << 0U;
constexpr TypeMask ArrayType = 1U << 1U;
constexpr TypeMask StringType = 1U << 2U;
constexpr TypeMask IntegerType = 1U << 3U;
constexpr TypeMask NumberType = 1U << 4U;
constexpr TypeMask BooleanType = 1U << 5U;
constexpr TypeMask NullType = 1U << 6U;

constexpr std::array<std::pair<std::string_view, TypeMask>, 7> TypeNames = {{
    {"object", ObjectType},
    {"array", ArrayType},
    {"string", StringType},
    {"integer", IntegerType},
    {"number", NumberType},
    {"boolean", BooleanType},
    {"null", NullType},
}};

TypeMask typeOf(const YamlNode& node) {
    if (node.isMapping()) {
        return ObjectType;
    }
    if (node.isSequence()) {
        return ArrayType;
    }
    switch (scalarType(node)) {
    case ScalarType::Null:
        return NullType;
    case ScalarType::Boolean:
        return BooleanType;
    case ScalarType::Integer:
        // Every integer is a number too.
        return IntegerType | NumberType;
    case ScalarType::Number:
        return NumberType;
    case ScalarType::String:
        return StringType;
    }
    return StringType;
}

std::string typeMaskText(TypeMask mask) {
    std::string text;
    for (const auto& [name, bit] : TypeNames) {
        if ((mask & bit) != 0) {
            text += text.empty() ? "" : " or ";
            text += name;
        }
    }
    return text;
}

// --- Schemas ---

struct SchemaNode {
    TypeMask types = 0; // 0 takes any type
    std::vector<std::pair<std::string, std::size_t>> properties;
    std::vector<std::pair<std::size_t, std::size_t>> patternProperties; // a pattern, the schema of the keys it matches
    std::vector<std::string> required;
    bool additionalAllowed = true;
    std::optional<std::size_t> additional;
    std::optional<std::size_t> items;
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::optional<double> exclusiveMinimum;
    std::optional<double> exclusiveMaximum;
    std::optional<std::size_t> minProperties;
    std::optional<std::size_t> propertyNames;
    std::optional<std::size_t> minItems;
    std::optional<std::size_t> maxItems;
    std::optional<std::size_t> minLength;
    std::optional<std::size_t> maxLength;
    std::optional<std::size_t> pattern;
    std::optional<std::vector<YamlNode>> allowed;
    std::optional<YamlNode> constant;
    std::optional<std::size_t> ref;
    std::vector<std::pair<std::string, std::string>> annotations;
};

struct CompiledSchema {
    std::vector<SchemaNode> nodes; // the root is node 0
    std::vector<SchemaPattern> patterns;
};

constexpr std::array<std::string_view, 5> IgnoredKeywords = {"title", "description", "default", "$schema", "$id"};
constexpr std::string_view DefsPrefix = "#/$defs/";

// Turns the schema's tree into SchemaNodes. Node 0 is the root; $defs get their numbers before any $ref is read.
class SchemaCompiler {
public:
    SchemaCompiler(const YamlNode& root, std::span<const std::string_view> annotationKeywords)
        : root_(&root), annotationKeywords_(annotationKeywords) {}

    Result<CompiledSchema> compile() {
        nodes_.emplace_back();
        if (!root_->isMapping()) {
            return fail("#: a schema must be a mapping");
        }
        std::vector<std::pair<std::size_t, const YamlEntry*>> definitions;
        if (const YamlNode* defs = root_->find("$defs")) {
            if (!defs->isMapping()) {
                return fail("#/$defs: must be a mapping");
            }
            for (const YamlEntry& entry : defs->entries()) {
                definitions.emplace_back(nodes_.size(), &entry);
                definitionIndexes_.emplace(entry.key, nodes_.size());
                nodes_.emplace_back();
            }
        }
        for (const auto& [index, entry] : definitions) {
            if (auto compiled = compileInto(index, entry->value, std::format("#/$defs/{}", entry->key)); !compiled) {
                return std::unexpected{compiled.error()};
            }
        }
        if (auto compiled = compileInto(0, *root_, "#"); !compiled) {
            return std::unexpected{compiled.error()};
        }
        if (auto acyclic = checkReferenceCycles(); !acyclic) {
            return std::unexpected{acyclic.error()};
        }
        return CompiledSchema{.nodes = std::move(nodes_), .patterns = std::move(patterns_)};
    }

private:
    Result<std::size_t> compileNew(const YamlNode& schema, const std::string& path) {
        const std::size_t index = nodes_.size();
        nodes_.emplace_back();
        if (auto compiled = compileInto(index, schema, path); !compiled) {
            return std::unexpected{compiled.error()};
        }
        return index;
    }

    static Result<std::size_t> readCount(const YamlNode& value, const std::string& path) {
        if (!value.isScalar() || scalarType(value) != ScalarType::Integer) {
            return fail(std::format("{}: must be a whole number of 0 or more", path));
        }
        std::size_t count = 0;
        const std::string& text = value.text();
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), count);
        if (error != std::errc{} || end != text.data() + text.size()) {
            return fail(std::format("{}: must be a whole number of 0 or more", path));
        }
        return count;
    }

    static Result<double> readLimit(const YamlNode& value, const std::string& path) {
        if (!value.isScalar() || !isNumeric(scalarType(value))) {
            return fail(std::format("{}: must be a number", path));
        }
        return numberValue(value, scalarType(value));
    }

    static Result<TypeMask> readTypes(const YamlNode& value, const std::string& path) {
        const auto typeBit = [&path](const YamlNode& name) -> Result<TypeMask> {
            for (const auto& [text, bit] : TypeNames) {
                if (name.isScalar() && name.text() == text) {
                    return bit;
                }
            }
            return fail(std::format("{}: unknown type '{}'", path, name.isScalar() ? name.text() : "?"));
        };
        if (value.isScalar()) {
            return typeBit(value);
        }
        if (!value.isSequence() || value.items().empty()) {
            return fail(std::format("{}: must be a type name or a list of them", path));
        }
        TypeMask mask = 0;
        for (const YamlNode& name : value.items()) {
            auto bit = typeBit(name);
            if (!bit) {
                return bit;
            }
            mask |= *bit;
        }
        return mask;
    }

    [[nodiscard]] Result<std::size_t> readReference(const YamlNode& value, const std::string& path) const {
        if (value.isScalar() && value.text() == "#") {
            return std::size_t{0};
        }
        if (value.isScalar() && value.text().starts_with(DefsPrefix)) {
            const auto found = definitionIndexes_.find(value.text().substr(DefsPrefix.size()));
            if (found != definitionIndexes_.end()) {
                return found->second;
            }
            return fail(std::format("{}: no definition '{}'", path, value.text()));
        }
        return fail(std::format("{}: only references to '#' and '{}<name>' are supported", path, DefsPrefix));
    }

    Result<void> compileInto(std::size_t index, const YamlNode& schema, const std::string& path) {
        if (!schema.isMapping()) {
            return fail(std::format("{}: a schema must be a mapping", path));
        }
        SchemaNode node;
        for (const YamlEntry& entry : schema.entries()) {
            auto read = readKeyword(node, entry, path);
            if (!read) {
                return read;
            }
        }
        nodes_[index] = std::move(node);
        return {};
    }

    // Each key of patternProperties is a pattern, compiled like the pattern keyword's, and its value the schema of the
    // properties whose names it matches.
    Result<void> readPatternProperties(SchemaNode& node, const YamlNode& value, const std::string& path) {
        if (!value.isMapping()) {
            return fail(formatMessage(messages::SchemaNotMapping, path));
        }
        for (const YamlEntry& property : value.entries()) {
            const std::string propertyPath = std::format("{}/{}", path, property.key);
            auto pattern = SchemaPattern::compile(property.key);
            if (!pattern) {
                return fail(formatMessage(messages::SchemaPatternInvalid, propertyPath, pattern.error().message));
            }
            auto child = compileNew(property.value, propertyPath);
            if (!child) {
                return std::unexpected{child.error()};
            }
            node.patternProperties.emplace_back(patterns_.size(), *child);
            patterns_.push_back(std::move(*pattern));
        }
        return {};
    }

    Result<void> readKeyword(SchemaNode& node, const YamlEntry& entry, const std::string& schemaPath) {
        const std::string& key = entry.key;
        const YamlNode& value = entry.value;
        const std::string path = std::format("{}/{}", schemaPath, key);
        if (std::ranges::contains(IgnoredKeywords, key)) {
            return {};
        }
        if (std::ranges::contains(annotationKeywords_, key)) {
            if (!value.isScalar() || scalarType(value) != ScalarType::String) {
                return fail(std::format("{}: must be a string", path));
            }
            node.annotations.emplace_back(key, value.text());
            return {};
        }
        if (key == "$defs") {
            return schemaPath == "#" ? Result<void>{}
                                     : fail(std::format("{}: $defs is only supported at the root", path));
        }
        if (key == "type") {
            auto types = readTypes(value, path);
            if (!types) {
                return std::unexpected{types.error()};
            }
            node.types = *types;
            return {};
        }
        if (key == "properties") {
            if (!value.isMapping()) {
                return fail(std::format("{}: must be a mapping", path));
            }
            for (const YamlEntry& property : value.entries()) {
                auto child = compileNew(property.value, std::format("{}/{}", path, property.key));
                if (!child) {
                    return std::unexpected{child.error()};
                }
                node.properties.emplace_back(property.key, *child);
            }
            return {};
        }
        if (key == "patternProperties") {
            return readPatternProperties(node, value, path);
        }
        if (key == "required") {
            if (!value.isSequence()) {
                return fail(std::format("{}: must be a list of keys", path));
            }
            for (const YamlNode& name : value.items()) {
                if (!name.isScalar()) {
                    return fail(std::format("{}: must be a list of keys", path));
                }
                node.required.push_back(name.text());
            }
            return {};
        }
        if (key == "additionalProperties") {
            if (value.isMapping()) {
                auto child = compileNew(value, path);
                if (!child) {
                    return std::unexpected{child.error()};
                }
                node.additional = *child;
                return {};
            }
            if (!value.isScalar() || scalarType(value) != ScalarType::Boolean) {
                return fail(std::format("{}: must be true, false or a schema", path));
            }
            node.additionalAllowed = isTrueText(value.text());
            return {};
        }
        if (key == "items") {
            auto child = compileNew(value, path);
            if (!child) {
                return std::unexpected{child.error()};
            }
            node.items = *child;
            return {};
        }
        if (key == "$ref") {
            auto target = readReference(value, path);
            if (!target) {
                return std::unexpected{target.error()};
            }
            node.ref = *target;
            return {};
        }
        if (key == "minimum" || key == "maximum" || key == "exclusiveMinimum" || key == "exclusiveMaximum") {
            auto limit = readLimit(value, path);
            if (!limit) {
                return std::unexpected{limit.error()};
            }
            if (key == "minimum") {
                node.minimum = *limit;
            } else if (key == "maximum") {
                node.maximum = *limit;
            } else {
                (key == "exclusiveMinimum" ? node.exclusiveMinimum : node.exclusiveMaximum) = *limit;
            }
            return {};
        }
        if (key == "minProperties") {
            auto count = readCount(value, path);
            if (!count) {
                return std::unexpected{count.error()};
            }
            node.minProperties = *count;
            return {};
        }
        if (key == "propertyNames") {
            auto child = compileNew(value, path);
            if (!child) {
                return std::unexpected{child.error()};
            }
            node.propertyNames = *child;
            return {};
        }
        if (key == "minItems" || key == "maxItems" || key == "minLength" || key == "maxLength") {
            auto count = readCount(value, path);
            if (!count) {
                return std::unexpected{count.error()};
            }
            if (key == "minItems") {
                node.minItems = *count;
            } else if (key == "maxItems") {
                node.maxItems = *count;
            } else {
                (key == "minLength" ? node.minLength : node.maxLength) = *count;
            }
            return {};
        }
        if (key == "pattern") {
            if (!value.isScalar() || scalarType(value) != ScalarType::String) {
                return fail(std::format("{}: must be a string", path));
            }
            auto pattern = SchemaPattern::compile(value.text());
            if (!pattern) {
                return fail(std::format("{}: {}", path, pattern.error().message));
            }
            node.pattern = patterns_.size();
            patterns_.push_back(std::move(*pattern));
            return {};
        }
        if (key == "enum") {
            if (!value.isSequence() || value.items().empty()) {
                return fail(std::format("{}: must be a list of values", path));
            }
            node.allowed.emplace(value.items().begin(), value.items().end());
            return {};
        }
        if (key == "const") {
            node.constant = value;
            return {};
        }
        return fail(std::format("{}: the keyword '{}' is not supported", schemaPath, key));
    }

    // A $ref that leads back to itself without going into the document would never end.
    [[nodiscard]] Result<void> checkReferenceCycles() const {
        for (std::size_t start = 0; start < nodes_.size(); ++start) {
            std::optional<std::size_t> at = nodes_[start].ref;
            for (std::size_t steps = 0; at; ++steps) {
                if (*at == start || steps > nodes_.size()) {
                    return fail("the schema has a cycle of $ref");
                }
                at = nodes_[*at].ref;
            }
        }
        return {};
    }

    const YamlNode* root_;
    std::span<const std::string_view> annotationKeywords_;
    std::map<std::string, std::size_t, std::less<>> definitionIndexes_;
    std::vector<SchemaNode> nodes_;
    std::vector<SchemaPattern> patterns_;
};

} // namespace

ScalarType scalarType(const YamlNode& scalar) {
    if (scalar.style() == YamlNode::ScalarStyle::Quoted) {
        return ScalarType::String;
    }
    const std::string& text = scalar.text();
    if (text.empty() || text == "~" || text == "null" || text == "Null" || text == "NULL") {
        return ScalarType::Null;
    }
    if (isTrueText(text) || text == "false" || text == "False" || text == "FALSE") {
        return ScalarType::Boolean;
    }
    if (isIntegerText(text)) {
        return ScalarType::Integer;
    }
    if (isNumberText(text)) {
        return ScalarType::Number;
    }
    return ScalarType::String;
}

struct JsonSchema::Compiled {
    CompiledSchema schema;
};

namespace {

// Walks a document and its schema together.
class SchemaChecker {
public:
    explicit SchemaChecker(const CompiledSchema& schema) : schema_(&schema) {}

    SchemaReport takeReport() { return std::move(report_); }

    void check(const YamlNode& value, std::size_t index, const std::string& path) {
        const SchemaNode& schema = schema_->nodes[index];
        if (schema.ref) {
            check(value, *schema.ref, path);
        }
        if (schema.types != 0 && (typeOf(value) & schema.types) == 0) {
            problem(value, path, std::format("expected {}, found {}", typeMaskText(schema.types), nodeTypeName(value)));
            return;
        }
        if (schema.allowed &&
            std::ranges::none_of(*schema.allowed, [&](const YamlNode& allowed) { return sameValue(value, allowed); })) {
            problem(value, path, "is not one of the allowed values");
        }
        if (schema.constant && !sameValue(value, *schema.constant)) {
            problem(value, path, "is not the required value");
        }
        if (value.isScalar()) {
            checkScalar(value, schema, path);
        } else if (value.isMapping()) {
            checkMapping(value, schema, path);
        } else {
            checkSequence(value, schema, path);
        }
    }

private:
    // A value checked against a $ref and against keywords next to it can break the same rule twice, such as a type
    // both give; it is reported once.
    void problem(const YamlNode& value, const std::string& path, std::string message) {
        const bool reported = std::ranges::any_of(report_.problems, [&](const SchemaProblem& known) {
            return known.line == value.line() && known.column == value.column() && known.path == path &&
                   known.message == message;
        });
        if (!reported) {
            report_.problems.push_back(
                {.path = path, .line = value.line(), .column = value.column(), .message = std::move(message)});
        }
    }

    void checkScalar(const YamlNode& value, const SchemaNode& schema, const std::string& path) {
        const ScalarType type = scalarType(value);
        if (isNumeric(type)) {
            const double number = numberValue(value, type);
            if (schema.minimum && number < *schema.minimum) {
                problem(value, path, std::format("{} is less than the minimum, {}", value.text(), *schema.minimum));
            }
            if (schema.maximum && number > *schema.maximum) {
                problem(value, path, std::format("{} is more than the maximum, {}", value.text(), *schema.maximum));
            }
            if (schema.exclusiveMinimum && number <= *schema.exclusiveMinimum) {
                problem(value, path, std::format("{} is not more than {}", value.text(), *schema.exclusiveMinimum));
            }
            if (schema.exclusiveMaximum && number >= *schema.exclusiveMaximum) {
                problem(value, path, std::format("{} is not less than {}", value.text(), *schema.exclusiveMaximum));
            }
        }
        if (type != ScalarType::String) {
            return;
        }
        // JSON Schema counts characters, not bytes: every UTF-8 byte but the continuation ones starts one.
        const auto length = static_cast<std::size_t>(std::ranges::count_if(
            value.text(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U; }));
        if (schema.minLength && length < *schema.minLength) {
            problem(value, path, std::format("has {} characters, fewer than {}", length, *schema.minLength));
        }
        if (schema.maxLength && length > *schema.maxLength) {
            problem(value, path, std::format("has {} characters, more than {}", length, *schema.maxLength));
        }
        if (schema.pattern && !schema_->patterns[*schema.pattern].search(value.text())) {
            problem(value, path,
                    std::format("'{}' does not match the pattern {}", value.text(),
                                schema_->patterns[*schema.pattern].source()));
        }
        for (const auto& [keyword, annotation] : schema.annotations) {
            report_.annotations.push_back({.keyword = keyword,
                                           .annotation = annotation,
                                           .value = value.text(),
                                           .path = path,
                                           .line = value.line(),
                                           .column = value.column()});
        }
    }

    void checkMapping(const YamlNode& value, const SchemaNode& schema, const std::string& path) {
        if (schema.minProperties && value.entries().size() < *schema.minProperties) {
            problem(value, path,
                    std::format("has {} keys, fewer than {}", value.entries().size(), *schema.minProperties));
        }
        for (const YamlEntry& entry : value.entries()) {
            const std::string childPath = std::format("{}.{}", path, entry.key);
            if (schema.propertyNames) {
                checkKey(entry, *schema.propertyNames, childPath);
            }
            if (checkDescribedProperty(entry, schema, childPath)) {
                continue;
            }
            if (schema.additional) {
                check(entry.value, *schema.additional, childPath);
            } else if (!schema.additionalAllowed) {
                problem(entry.value, path, std::format("the key '{}' is not allowed", entry.key));
            }
        }
        for (const std::string& key : schema.required) {
            if (value.find(key) == nullptr) {
                problem(value, path, std::format("the key '{}' is missing", key));
            }
        }
    }

    // Checks a property against the schemas that describe its key: its entry in properties and, as in draft 2020-12,
    // every entry of patternProperties whose pattern matches the key. Whether any did; additionalProperties takes the
    // rest.
    bool checkDescribedProperty(const YamlEntry& entry, const SchemaNode& schema, const std::string& path) {
        bool described = false;
        const auto property =
            std::ranges::find(schema.properties, entry.key, &std::pair<std::string, std::size_t>::first);
        if (property != schema.properties.end()) {
            check(entry.value, property->second, path);
            described = true;
        }
        for (const auto& [pattern, child] : schema.patternProperties) {
            if (schema_->patterns[pattern].search(entry.key)) {
                check(entry.value, child, path);
                described = true;
            }
        }
        return described;
    }

    // A key checked as the string it is, with its problems placed at its value (keys keep no position).
    void checkKey(const YamlEntry& entry, std::size_t schema, const std::string& path) {
        const std::size_t before = report_.problems.size();
        check(YamlNode::scalar(entry.key, YamlNode::ScalarStyle::Quoted), schema, path);
        for (std::size_t i = before; i < report_.problems.size(); ++i) {
            report_.problems[i].line = entry.value.line();
            report_.problems[i].column = entry.value.column();
            report_.problems[i].message = std::format("the key: {}", report_.problems[i].message);
        }
    }

    void checkSequence(const YamlNode& value, const SchemaNode& schema, const std::string& path) {
        const std::size_t count = value.items().size();
        if (schema.minItems && count < *schema.minItems) {
            problem(value, path, std::format("has {} items, fewer than {}", count, *schema.minItems));
        }
        if (schema.maxItems && count > *schema.maxItems) {
            problem(value, path, std::format("has {} items, more than {}", count, *schema.maxItems));
        }
        if (schema.items) {
            for (std::size_t i = 0; i < count; ++i) {
                check(value.items()[i], *schema.items, std::format("{}[{}]", path, i));
            }
        }
    }

    const CompiledSchema* schema_;
    SchemaReport report_;
};

} // namespace

Result<JsonSchema> JsonSchema::compile(const YamlNode& schema, std::span<const std::string_view> annotationKeywords) {
    auto compiled = SchemaCompiler{schema, annotationKeywords}.compile();
    if (!compiled) {
        return std::unexpected{compiled.error()};
    }
    return JsonSchema{std::make_unique<Compiled>(Compiled{.schema = std::move(*compiled)})};
}

JsonSchema::JsonSchema(std::unique_ptr<Compiled> compiled) : compiled_(std::move(compiled)) {}
JsonSchema::JsonSchema(JsonSchema&&) noexcept = default;
JsonSchema& JsonSchema::operator=(JsonSchema&&) noexcept = default;
JsonSchema::~JsonSchema() = default;

SchemaReport JsonSchema::check(const YamlNode& document) const {
    SchemaChecker checker{compiled_->schema};
    checker.check(document, 0, "$");
    return checker.takeReport();
}

} // namespace dwe::core
