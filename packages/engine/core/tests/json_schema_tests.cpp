#include <dwe/core/json_schema.hpp>
#include <dwe/core/yaml.hpp>

#include <doctest/doctest.h>

#include <array>
#include <format>
#include <string>
#include <string_view>
#include <vector>

using dwe::core::JsonSchema;
using dwe::core::parseYaml;
using dwe::core::ScalarType;
using dwe::core::scalarType;
using dwe::core::SchemaReport;
using dwe::core::YamlNode;

namespace {

// The message of a failed result. doctest builds a message even when the check passes, so it cannot
// call error().
template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

YamlNode parsed(std::string_view text) {
  auto node = parseYaml(text);
  REQUIRE_MESSAGE(node.has_value(), errorOf(node));
  return std::move(*node);
}

JsonSchema compiled(std::string_view schema, std::span<const std::string_view> annotations = {}) {
  auto compiled = JsonSchema::compile(parsed(schema), annotations);
  REQUIRE_MESSAGE(compiled.has_value(), errorOf(compiled));
  return std::move(*compiled);
}

std::string compileError(std::string_view schema) {
  const auto compiled = JsonSchema::compile(parsed(schema));
  REQUIRE_FALSE(compiled.has_value());
  return errorOf(compiled);
}

// The problems of a document as "path: message" lines.
std::vector<std::string> problems(const JsonSchema& schema, std::string_view document) {
  std::vector<std::string> lines;
  for (const auto& problem : schema.check(parsed(document)).problems) {
    lines.push_back(problem.path + ": " + problem.message);
  }
  return lines;
}

bool matches(std::string_view pattern, std::string_view text) {
  const JsonSchema schema =
      compiled(std::string{R"({"pattern": ")"} + std::string{pattern} + "\"}");
  return schema.check(YamlNode::scalar(std::string{text}, YamlNode::ScalarStyle::Quoted))
      .problems.empty();
}

ScalarType typeOf(std::string_view text) {
  return scalarType(YamlNode::scalar(std::string{text}));
}

} // namespace

TEST_CASE("plain scalars take the types of YAML's core schema and quoted ones are strings") {
  CHECK(typeOf("") == ScalarType::Null);
  CHECK(typeOf("~") == ScalarType::Null);
  CHECK(typeOf("null") == ScalarType::Null);
  CHECK(typeOf("true") == ScalarType::Boolean);
  CHECK(typeOf("FALSE") == ScalarType::Boolean);
  CHECK(typeOf("42") == ScalarType::Integer);
  CHECK(typeOf("-7") == ScalarType::Integer);
  CHECK(typeOf("0x00fe") == ScalarType::Integer);
  CHECK(typeOf("0o17") == ScalarType::Integer);
  CHECK(typeOf("1.5") == ScalarType::Number);
  CHECK(typeOf("-2e3") == ScalarType::Number);
  CHECK(typeOf(".inf") == ScalarType::Number);
  CHECK(typeOf("1.0.0") == ScalarType::String);
  CHECK(typeOf("agumon") == ScalarType::String);
  CHECK(typeOf("0x") == ScalarType::String);
  CHECK(scalarType(YamlNode::scalar("42", YamlNode::ScalarStyle::Quoted)) == ScalarType::String);
}

TEST_CASE("a schema checks types, properties, required keys and extra keys") {
  const JsonSchema schema = compiled(R"({
        "type": "object",
        "required": ["id", "level"],
        "additionalProperties": false,
        "properties": {
            "id": {"type": "string"},
            "level": {"type": "integer"},
            "drop": {"type": ["string", "null"]},
            "ratio": {"type": "number"}
        }
    })");
  CHECK(problems(schema, "id: agumon\nlevel: 3\ndrop: ~\nratio: 3\n").empty());
  CHECK(problems(schema, "id: \"12\"\nlevel: 3\nratio: 0.5\n").empty());
  CHECK(problems(schema, "id: 12\nlevel: three\n") ==
        std::vector<std::string>{"$.id: expected string, found integer",
                                 "$.level: expected integer, found string"});
  CHECK(problems(schema, "id: a\nlevel: 1\nextra: 1\n") ==
        std::vector<std::string>{"$: the key 'extra' is not allowed"});
  CHECK(problems(schema, "id: a\n") == std::vector<std::string>{"$: the key 'level' is missing"});
  CHECK(problems(schema, "- 1\n") == std::vector<std::string>{"$: expected object, found array"});
}

TEST_CASE(
    "patternProperties check the keys their patterns match, next to properties and before extra keys") {
  const JsonSchema schema = compiled(R"({
        "type": "object",
        "additionalProperties": false,
        "properties": {"name": {"type": "string"}, "10": {"maximum": 50}},
        "patternProperties": {"^[0-9]+$": {"type": "integer"}, "^1": {"minimum": 10}}
    })");
  CHECK(problems(schema, "name: a\n1: 12\n7: 3\n10: 20\n").empty());
  CHECK(problems(schema, "12: twelve\n") ==
        std::vector<std::string>{"$.12: expected integer, found string"});
  CHECK(problems(schema, "1: 5\n") ==
        std::vector<std::string>{"$.1: 5 is less than the minimum, 10"});
  CHECK(problems(schema, "10: 60\n") ==
        std::vector<std::string>{"$.10: 60 is more than the maximum, 50"});
  CHECK(problems(schema, "x1: 3\n") == std::vector<std::string>{"$: the key 'x1' is not allowed"});

  const JsonSchema extra = compiled(R"({
        "patternProperties": {"^a": {"type": "string"}},
        "additionalProperties": {"type": "integer"}
    })");
  CHECK(problems(extra, "ab: x\nb: 1\n").empty());
  CHECK(problems(extra, "ab: 1\nb: x\n") ==
        std::vector<std::string>{"$.ab: expected string, found integer",
                                 "$.b: expected integer, found string"});

  // Unanchored, a pattern matches anywhere in the key.
  const JsonSchema unanchored = compiled(R"({"patternProperties": {"b": {"type": "integer"}}})");
  CHECK(problems(unanchored, "ab: 1\nbc: 2\nac: x\n").empty());
  CHECK(problems(unanchored, "ab: x\n") ==
        std::vector<std::string>{"$.ab: expected integer, found string"});
}

TEST_CASE("patternProperties match in linear time and refuse what the patterns refuse") {
  // YAML keeps a key under 1024 characters; a backtracking matcher would never finish these at a
  // tenth of that.
  const JsonSchema schema = compiled(
      R"({"patternProperties": {"^(a*)*b$": {"type": "string"}, "^(a|aa)+$": {"type": "integer"}}})");
  const std::string key(1000, 'a');
  CHECK(problems(schema, key + ": x\n") ==
        std::vector<std::string>{std::format("$.{}: expected integer, found string", key)});
  CHECK(compileError(R"({"patternProperties": ["^a"]})") ==
        "#/patternProperties: must be a mapping");
  CHECK(compileError(R"({"patternProperties": {"a.c": {}}})")
            .starts_with("#/patternProperties/a.c: "));
  CHECK(compileError(R"({"patternProperties": {"^a": {"format": "uri"}}})") ==
        "#/patternProperties/^a: the keyword 'format' is not supported");
}

TEST_CASE("a schema checks items, counts, limits, enums and constants") {
  const JsonSchema schema = compiled(R"({
        "type": "object",
        "additionalProperties": {"type": "integer", "minimum": -1, "maximum": 255},
        "properties": {
            "special": {"type": "array", "minItems": 1, "maxItems": 2, "items": {"enum": [0, "fire", null]}},
            "version": {"const": 7},
            "cell": {"type": "string", "minLength": 1, "maxLength": 1}
        }
    })");
  CHECK(problems(schema, "special: [0, fire]\nversion: 7\nhp: 255\nmp: -1\n").empty());
  CHECK(problems(schema, "special: []\n") ==
        std::vector<std::string>{"$.special: has 0 items, fewer than 1"});
  CHECK(problems(schema, "special: [0, 0, 0]\n") ==
        std::vector<std::string>{"$.special: has 3 items, more than 2"});
  CHECK(problems(schema, "special: [\"0\", water]\n") ==
        std::vector<std::string>{"$.special[0]: is not one of the allowed values",
                                 "$.special[1]: is not one of the allowed values"});
  CHECK(problems(schema, "version: 0x07\n").empty());
  CHECK(problems(schema, "cell: \"\xc3\xa9\"\n").empty());
  CHECK(problems(schema, "cell: \"\"\n") ==
        std::vector<std::string>{"$.cell: has 0 characters, fewer than 1"});
  CHECK(problems(schema, "cell: ab\n") ==
        std::vector<std::string>{"$.cell: has 2 characters, more than 1"});
  CHECK(problems(schema, "version: \"7\"\n") ==
        std::vector<std::string>{"$.version: is not the required value"});
  CHECK(problems(schema, "hp: 256\nmp: -2\n") ==
        std::vector<std::string>{"$.hp: 256 is more than the maximum, 255",
                                 "$.mp: -2 is less than the minimum, -1"});
}

TEST_CASE("a schema checks exclusive limits, the number of keys and the keys' names") {
  const JsonSchema schema = compiled(R"({
        "type": "object",
        "properties": {
            "speed": {"type": "number", "exclusiveMinimum": 0, "exclusiveMaximum": 10},
            "clips": {
                "type": "object",
                "minProperties": 1,
                "propertyNames": {"pattern": "^[a-z_]+$"},
                "additionalProperties": {"type": "integer"}
            }
        }
    })");
  CHECK(problems(schema, "speed: 0.5\nclips: {idle: 0, walk_fast: 2}\n").empty());
  CHECK(problems(schema, "speed: 0\n") ==
        std::vector<std::string>{"$.speed: 0 is not more than 0"});
  CHECK(problems(schema, "speed: 10\n") ==
        std::vector<std::string>{"$.speed: 10 is not less than 10"});
  CHECK(problems(schema, "clips: {}\n") ==
        std::vector<std::string>{"$.clips: has 0 keys, fewer than 1"});
  CHECK(
      problems(schema, "clips: {Idle: 0, \"2\": 1}\n") ==
      std::vector<std::string>{"$.clips.Idle: the key: 'Idle' does not match the pattern ^[a-z_]+$",
                               "$.clips.2: the key: '2' does not match the pattern ^[a-z_]+$"});
  // A key's problem points at its value: keys keep no position of their own.
  const auto report = schema.check(parsed("clips:\n  Idle: 0\n"));
  REQUIRE(report.problems.size() == 1);
  CHECK(report.problems[0].line == 2);
}

TEST_CASE("a schema follows references to its definitions and to itself") {
  const JsonSchema schema = compiled(R"({
        "$defs": {
            "id": {"type": "string", "pattern": "^[a-z]+$"},
            "node": {"type": "object", "properties": {"name": {"$ref": "#/$defs/id"}, "children": {"$ref": "#"}}}
        },
        "type": "array",
        "items": {"$ref": "#/$defs/node"}
    })");
  CHECK(problems(schema, "- name: root\n  children:\n  - name: leaf\n").empty());
  CHECK(problems(schema, "- name: root\n  children:\n  - name: Leaf\n") ==
        std::vector<std::string>{
            "$[0].children[0].name: 'Leaf' does not match the pattern ^[a-z]+$"});
  // A rule that both the reference and the keywords next to it give is broken once.
  const JsonSchema twice =
      compiled(R"({"$defs": {"id": {"type": "string"}}, "$ref": "#/$defs/id", "type": "string"})");
  CHECK(problems(twice, "[1]\n") == std::vector<std::string>{"$: expected string, found array"});
}

TEST_CASE("problems point at the line and column of the value") {
  const JsonSchema schema = compiled(R"({"properties": {"level": {"type": "integer"}}})");
  const SchemaReport report = schema.check(parsed("id: a\nlevel: high\n"));
  REQUIRE(report.problems.size() == 1);
  CHECK(report.problems[0].line == 2);
  CHECK(report.problems[0].column == 8);
}

TEST_CASE("annotation keywords report the strings they apply to") {
  constexpr std::array<std::string_view, 1> Keywords = {"assetReference"};
  const JsonSchema schema = compiled(R"({
        "properties": {
            "file": {"type": "string", "assetReference": "./{}"},
            "target": {"type": ["string", "null"], "assetReference": "map/{}/data"}
        }
    })",
                                     Keywords);
  const SchemaReport report = schema.check(parsed("file: bank.sf2\ntarget: ~\n"));
  CHECK(report.problems.empty());
  REQUIRE(report.annotations.size() == 1);
  CHECK(report.annotations[0].keyword == "assetReference");
  CHECK(report.annotations[0].annotation == "./{}");
  CHECK(report.annotations[0].value == "bank.sf2");
  CHECK(report.annotations[0].path == "$.file");
  CHECK(compileError(R"({"assetReference": "x"})").contains("'assetReference' is not supported"));
}

TEST_CASE("keywords outside the subset are refused when compiling") {
  CHECK(compileError(R"({"oneOf": []})") == "#: the keyword 'oneOf' is not supported");
  CHECK(compileError(R"({"properties": {"a": {"format": "uri"}}})") ==
        "#/properties/a: the keyword 'format' is not supported");
  CHECK(compileError(R"({"items": [{"type": "integer"}]})") ==
        "#/items: a schema must be a mapping");
  CHECK(compileError(R"({"type": "decimal"})") == "#/type: unknown type 'decimal'");
  CHECK(compileError(R"({"$ref": "other.json#/x"})").contains("only references to '#'"));
  CHECK(compileError(R"({"$ref": "#/$defs/missing"})").contains("no definition"));
  CHECK(compileError(R"({"$defs": {"a": {"$ref": "#/$defs/b"}, "b": {"$ref": "#/$defs/a"}}})") ==
        "the schema has a cycle of $ref");
  CHECK(compileError(R"({"properties": {"a": {"$defs": {}}}})")
            .contains("only supported at the root"));
  CHECK(compileError(R"({"minItems": -1})").contains("whole number"));
}

TEST_CASE("patterns match anywhere unless anchored") {
  CHECK(matches("^(0|[1-9][0-9]*)\\\\.(0|[1-9][0-9]*)\\\\.(0|[1-9][0-9]*)$", "1.20.0"));
  CHECK_FALSE(matches("^(0|[1-9][0-9]*)\\\\.(0|[1-9][0-9]*)\\\\.(0|[1-9][0-9]*)$", "1.02.0"));
  CHECK_FALSE(matches("^(0|[1-9][0-9]*)\\\\.(0|[1-9][0-9]*)\\\\.(0|[1-9][0-9]*)$", "1x2.0"));
  CHECK(matches("^-?[0-9]+( -?[0-9]+)*$", "12 -1 0"));
  CHECK_FALSE(matches("^-?[0-9]+( -?[0-9]+)*$", "12  1"));
  CHECK(matches("^[a-z0-9_]{1,4}$", "ab_1"));
  CHECK_FALSE(matches("^[a-z0-9_]{1,4}$", "ab_12"));
  CHECK_FALSE(matches("^[a-z0-9_]{1,4}$", ""));
  CHECK(matches("b+", "abbc"));
  CHECK(matches("x{2,}", "axxx"));
  CHECK_FALSE(matches("x{2}y", "axy"));
  CHECK(matches("colou?r|grey", "a grey cat"));
  CHECK_FALSE(matches("^[a-z]$", "\xc3\xa9"));
}

TEST_CASE("patterns run in linear time, even those that backtrack forever elsewhere") {
  const std::string text(5000, 'a');
  CHECK_FALSE(matches("^(a*)*b$", text));
  CHECK(matches("^(a|aa)+$", text));
}

TEST_CASE("pattern features outside the subset are refused") {
  const auto patternError = [](std::string_view pattern) {
    return compileError(std::string{R"({"pattern": ")"} + std::string{pattern} + "\"}");
  };
  CHECK(patternError("a.c").contains("'.' is not supported"));
  CHECK(patternError("\\\\d+").contains("only punctuation can be escaped"));
  CHECK(patternError("[^a]").contains("negated classes"));
  CHECK(patternError("(?:a)").contains("'(?' groups"));
  CHECK(patternError("a+?").contains("lazy"));
  CHECK(patternError("(a").contains("missing ')'"));
  CHECK(patternError("a)").contains("unexpected ')'"));
  CHECK(patternError("[z-a]").contains("ends before it starts"));
  CHECK(patternError("a{2000}").contains("up to 1000"));
  CHECK(patternError("^*").contains("cannot be repeated"));
  CHECK(patternError("(((((((((((((((((((((((((((((((((a)))))))))))))))))))))))))))))))))")
            .contains("nested"));
  CHECK(patternError("(a{1000}){1000}").contains("too large"));
}
