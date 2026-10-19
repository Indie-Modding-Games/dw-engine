#pragma once

#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// A small subset of JSON Schema, enough to check that the documents formats emits match content/schemas: type,
// properties, required, additionalProperties: false, items, minItems, maxItems, enum, const and local $refs. The
// schema files are JSON, which the YAML parser reads.
namespace dwe::formats::test {

inline std::string readSchemaText(std::string_view name) {
    std::ifstream file{std::format("{}/{}.schema.json", DWE_SCHEMA_DIR, name)};
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

class SchemaChecker {
public:
    explicit SchemaChecker(const core::YamlNode& root) : root_(&root) {}

    // Returns one line per problem; empty when the document matches.
    std::vector<std::string> check(const core::YamlNode& document) {
        problems_.clear();
        checkNode(document, *root_, "$");
        return problems_;
    }

private:
    const core::YamlNode* resolve(const core::YamlNode* schema) {
        const core::YamlNode* ref = schema->find("$ref");
        if (ref == nullptr) {
            return schema;
        }
        constexpr std::string_view Prefix = "#/$defs/";
        const core::YamlNode* defs = root_->find("$defs");
        const core::YamlNode* target = defs != nullptr && ref->text().starts_with(Prefix)
                                           ? defs->find(ref->text().substr(Prefix.size()))
                                           : nullptr;
        if (target == nullptr) {
            problems_.push_back(std::format("unresolved $ref {}", ref->text()));
            return schema;
        }
        return target;
    }

    static bool isInteger(const core::YamlNode& node) {
        const std::string& text = node.text();
        const std::string_view digits = text.starts_with('-') ? std::string_view{text}.substr(1) : text;
        return node.isScalar() && node.style() == core::YamlNode::ScalarStyle::Plain && !digits.empty() &&
               std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; });
    }

    static bool hasType(const core::YamlNode& node, std::string_view type) {
        const bool plain = node.isScalar() && node.style() == core::YamlNode::ScalarStyle::Plain;
        if (type == "object") {
            return node.isMapping();
        }
        if (type == "array") {
            return node.isSequence();
        }
        if (type == "string") {
            return node.isScalar() && node.style() == core::YamlNode::ScalarStyle::Quoted;
        }
        if (type == "integer") {
            return isInteger(node);
        }
        if (type == "boolean") {
            return plain && (node.text() == "true" || node.text() == "false");
        }
        if (type == "null") {
            return plain && node.text() == "null";
        }
        return false;
    }

    void checkNode(const core::YamlNode& node, const core::YamlNode& schemaRef, const std::string& path) {
        const core::YamlNode& schema = *resolve(&schemaRef);
        if (const core::YamlNode* type = schema.find("type")) {
            const bool matches = type->isScalar() ? hasType(node, type->text())
                                                  : std::ranges::any_of(type->items(), [&](const core::YamlNode& t) {
                                                        return hasType(node, t.text());
                                                    });
            if (!matches) {
                problems_.push_back(std::format("{}: wrong type", path));
                return;
            }
        }
        if (const core::YamlNode* values = schema.find("enum")) {
            if (!node.isScalar() || std::ranges::none_of(values->items(), [&](const core::YamlNode& v) {
                    return v.text() == node.text();
                })) {
                problems_.push_back(std::format("{}: not one of the enum values", path));
            }
        }
        if (const core::YamlNode* value = schema.find("const")) {
            if (!node.isScalar() || value->text() != node.text()) {
                problems_.push_back(std::format("{}: not the const value", path));
            }
        }
        if (node.isMapping()) {
            checkMapping(node, schema, path);
        }
        if (node.isSequence()) {
            checkSequence(node, schema, path);
        }
    }

    void checkMapping(const core::YamlNode& node, const core::YamlNode& schema, const std::string& path) {
        const core::YamlNode* properties = schema.find("properties");
        const core::YamlNode* additional = schema.find("additionalProperties");
        const bool closed = additional != nullptr && additional->isScalar() && additional->text() == "false";
        for (const core::YamlEntry& entry : node.entries()) {
            const core::YamlNode* property = properties != nullptr ? properties->find(entry.key) : nullptr;
            if (property != nullptr) {
                checkNode(entry.value, *property, std::format("{}.{}", path, entry.key));
            } else if (closed) {
                problems_.push_back(std::format("{}: key '{}' is not in the schema", path, entry.key));
            }
        }
        if (const core::YamlNode* required = schema.find("required")) {
            for (const core::YamlNode& key : required->items()) {
                if (node.find(key.text()) == nullptr) {
                    problems_.push_back(std::format("{}: missing key '{}'", path, key.text()));
                }
            }
        }
    }

    void checkSequence(const core::YamlNode& node, const core::YamlNode& schema, const std::string& path) {
        const std::size_t count = node.items().size();
        if (const core::YamlNode* min = schema.find("minItems"); min != nullptr && count < std::stoul(min->text())) {
            problems_.push_back(std::format("{}: fewer than {} items", path, min->text()));
        }
        if (const core::YamlNode* max = schema.find("maxItems"); max != nullptr && count > std::stoul(max->text())) {
            problems_.push_back(std::format("{}: more than {} items", path, max->text()));
        }
        if (const core::YamlNode* items = schema.find("items")) {
            for (std::size_t i = 0; i < count; ++i) {
                checkNode(node.items()[i], *items, std::format("{}[{}]", path, i));
            }
        }
    }

    const core::YamlNode* root_;
    std::vector<std::string> problems_;
};

// Checks a document against content/schemas/<name>.schema.json; returns the problems, or why the schema could not
// be read.
inline std::vector<std::string> checkAgainstSchema(const core::YamlNode& document, std::string_view name) {
    const auto schema = core::parseYaml(readSchemaText(name));
    if (!schema || !schema->isMapping()) {
        return {std::format("schema '{}' could not be read", name)};
    }
    return SchemaChecker{*schema}.check(document);
}

} // namespace dwe::formats::test
