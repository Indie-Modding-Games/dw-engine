#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::core {

// What a YAML scalar means to a schema, after the core schema of YAML 1.2: quoted scalars are
// strings; plain ones are null (empty, ~, null), booleans (true, false), integers (decimal, 0x
// hexadecimal, 0o octal), numbers (with a fraction or an exponent, .inf, .nan) or, if nothing else,
// strings.
enum class ScalarType : std::uint8_t { Null, Boolean, Integer, Number, String };

// Resolves the type of a scalar node.
ScalarType scalarType(const YamlNode& scalar);

// A place in a document where it does not match its schema.
struct SchemaProblem {
  std::string path; // $ for the root, then .key and [index]
  std::size_t line = 0;
  std::size_t column = 0;
  std::string message;
};

// A string of the document that an annotation keyword of its schema marks, such as a reference to
// an asset.
struct SchemaAnnotation {
  std::string keyword;
  std::string annotation; // the keyword's text in the schema
  std::string value;      // the string in the document
  std::string path;
  std::size_t line = 0;
  std::size_t column = 0;
};

// What checking a document found: the problems and the annotated strings, in document order.
struct SchemaReport {
  std::vector<SchemaProblem> problems;
  std::vector<SchemaAnnotation> annotations;
};

// A JSON Schema, compiled once and used to check any number of YAML documents. Only a subset of
// draft 2020-12 is supported: type, properties, patternProperties, required, additionalProperties,
// minProperties, propertyNames, items, $ref to "#" or "#/$defs/<name>" with $defs at the root,
// minimum, maximum, exclusiveMinimum, exclusiveMaximum, minItems, maxItems, minLength, maxLength,
// pattern, enum and const, plus the annotations title, description, default, $schema and $id. A
// property follows its entry in properties and every patternProperties entry whose pattern it
// matches; additionalProperties takes only the properties that none of them describes. Patterns
// (pattern and the keys of patternProperties) take literals, escaped punctuation, [] classes with
// ranges, groups with |, the quantifiers ?
// * + {n} {n,} {n,m} and the anchors ^ and $, and match in time linear in the text. Anything else
// is an error when compiling, so a schema never checks less than it says.
class JsonSchema {
public:
  // Compiles a schema read with parseYaml (JSON is YAML). The annotation keywords are extra
  // keywords whose string value check() reports for every string of the document they apply to.
  static Result<JsonSchema> compile(const YamlNode& schema,
                                    std::span<const std::string_view> annotationKeywords = {});

  JsonSchema(JsonSchema&&) noexcept;
  JsonSchema& operator=(JsonSchema&&) noexcept;
  JsonSchema(const JsonSchema&) = delete;
  JsonSchema& operator=(const JsonSchema&) = delete;
  ~JsonSchema();

  // Checks a document against the schema.
  [[nodiscard]] SchemaReport check(const YamlNode& document) const;

private:
  struct Compiled;

  explicit JsonSchema(std::unique_ptr<Compiled> compiled);

  std::unique_ptr<Compiled> compiled_;
};

} // namespace dwe::core
