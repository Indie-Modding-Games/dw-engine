#pragma once

#include <dwe/core/json_schema.hpp>
#include <dwe/core/yaml.hpp>

#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Checks that the documents formats emits match content/schemas, with the engine's JSON Schema validator.
namespace dwe::formats::test {

inline std::string readSchemaText(std::string_view name) {
    std::ifstream file{std::format("{}/{}.schema.json", DWE_SCHEMA_DIR, name)};
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

// Checks a document against content/schemas/<name>.schema.json; returns one "path: problem" line per problem, or
// why the schema could not be used.
inline std::vector<std::string> checkAgainstSchema(const core::YamlNode& document, std::string_view name) {
    const auto text = core::parseYaml(readSchemaText(name));
    if (!text) {
        return {std::format("schema '{}' could not be read", name)};
    }
    const auto schema = core::JsonSchema::compile(*text);
    if (!schema) {
        return {std::format("schema '{}': {}", name, schema.error().message)};
    }
    std::vector<std::string> problems;
    for (const core::SchemaProblem& problem : schema->check(document).problems) {
        problems.push_back(std::format("{}: {}", problem.path, problem.message));
    }
    return problems;
}

} // namespace dwe::formats::test
