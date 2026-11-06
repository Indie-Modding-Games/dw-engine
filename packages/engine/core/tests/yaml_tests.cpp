#include <dwe/core/yaml.hpp>

#include <doctest/doctest.h>

#include <string>
#include <string_view>

using dwe::core::emitYaml;
using dwe::core::parseYaml;
using dwe::core::YamlLimits;
using dwe::core::YamlNode;

namespace {

YamlNode parsed(std::string_view text) {
  auto node = parseYaml(text);
  REQUIRE_MESSAGE(node.has_value(), node.error().message);
  return std::move(*node);
}

std::string emitted(const YamlNode& node) {
  auto text = emitYaml(node);
  REQUIRE_MESSAGE(text.has_value(), text.error().message);
  return std::move(*text);
}

std::string errorOf(std::string_view text, const YamlLimits& limits = {}) {
  const auto node = parseYaml(text, limits);
  REQUIRE_FALSE(node.has_value());
  return node.error().message;
}

} // namespace

TEST_CASE("yaml reads mappings, sequences and scalars in document order") {
  const YamlNode root =
      parsed("id: base\nversion: \"1.0.0\"\ntags: [a, b]\nnested:\n  zeta: 1\n  alpha: 2\n");
  REQUIRE(root.isMapping());
  REQUIRE(root.entries().size() == 4);
  CHECK(root.entries()[0].key == "id");
  CHECK(root.entries()[3].key == "nested");

  CHECK(root.find("id")->text() == "base");
  CHECK(root.find("id")->style() == YamlNode::ScalarStyle::Plain);
  CHECK(root.find("version")->style() == YamlNode::ScalarStyle::Quoted);
  CHECK(root.find("missing") == nullptr);

  const YamlNode* tags = root.find("tags");
  REQUIRE(tags->isSequence());
  REQUIRE(tags->items().size() == 2);
  CHECK(tags->items()[1].text() == "b");

  const YamlNode* nested = root.find("nested");
  REQUIRE(nested->entries().size() == 2);
  CHECK(nested->entries()[0].key == "zeta");
  CHECK(nested->entries()[1].key == "alpha");
  CHECK(nested->line() == 5);
  CHECK(nested->find("alpha")->line() == 6);
  CHECK(nested->find("alpha")->column() == 10);
}

TEST_CASE("yaml reads an empty text as an empty scalar") {
  const YamlNode root = parsed("");
  REQUIRE(root.isScalar());
  CHECK(root.text().empty());
}

TEST_CASE("yaml writes a fixed layout") {
  YamlNode root = YamlNode::mapping();
  root.set("id", YamlNode::scalar("base"));
  root.set("version", YamlNode::scalar("1.0.0", YamlNode::ScalarStyle::Quoted));
  YamlNode list = YamlNode::sequence();
  list.append(YamlNode::scalar("one"));
  list.append(YamlNode::scalar("needs: quotes"));
  root.set("list", std::move(list));
  YamlNode nested = YamlNode::mapping();
  nested.set("b", YamlNode::scalar("2"));
  nested.set("a", YamlNode::scalar("line\nbreak", YamlNode::ScalarStyle::Quoted));
  root.set("nested", std::move(nested));
  root.set("empty", YamlNode::sequence());

  CHECK(emitted(root) == "id: base\n"
                         "version: \"1.0.0\"\n"
                         "list:\n"
                         "- one\n"
                         "- 'needs: quotes'\n"
                         "nested:\n"
                         "  b: 2\n"
                         "  a: \"line\\nbreak\"\n"
                         "empty: []\n");
}

TEST_CASE("yaml keeps the place of a key that is set again") {
  YamlNode root = YamlNode::mapping();
  root.set("first", YamlNode::scalar("1"));
  root.set("second", YamlNode::scalar("2"));
  root.set("first", YamlNode::scalar("3"));
  CHECK(emitted(root) == "first: 3\nsecond: 2\n");
}

TEST_CASE("yaml round-trips through the emitter") {
  constexpr std::string_view Text = "name: \"Some pack\"\n"
                                    "plain: text with spaces\n"
                                    "quoted: \"yes\"\n"
                                    "unicode: \"ñandú ☃\"\n"
                                    "deps:\n"
                                    "- id: base\n"
                                    "  min: \"1.2.0\"\n"
                                    "- id: other\n"
                                    "  min: \"0.1.0\"\n"
                                    "flow: {x: 1, y: [2, 3]}\n"
                                    "empty: {}\n"
                                    "blank:\n";
  const YamlNode first = parsed(Text);
  const std::string once = emitted(first);
  const YamlNode second = parsed(once);
  CHECK(second == first);
  CHECK(emitted(second) == once);
  CHECK(second.find("quoted")->style() == YamlNode::ScalarStyle::Quoted);
  CHECK(second.find("unicode")->text() == "ñandú ☃");
  CHECK(second.find("blank")->text().empty());
}

TEST_CASE("yaml compares trees by content") {
  CHECK(parsed("a: [1, 2]") == parsed("a:\n- 1\n- 2\n"));
  CHECK_FALSE(parsed("a: 1\nb: 2") == parsed("b: 2\na: 1"));
  CHECK_FALSE(parsed("a: 1") == parsed("[a, 1]"));
}

TEST_CASE("yaml reports syntax errors with their position") {
  CHECK(errorOf("key: [1, 2\n").starts_with("line 2, column 1:"));
  CHECK(errorOf("a: 1\n b: 2\n").starts_with("line 2, column 3:"));
  CHECK(errorOf("a: \"open\n").find("line ") != std::string::npos);
  CHECK(errorOf("\xff\xfe\x00").starts_with("byte "));
}

TEST_CASE("yaml rejects what the project does not use") {
  CHECK(errorOf("a: 1\na: 2\n") == "line 2, column 1: duplicate key 'a'");
  CHECK(errorOf("? [a]\n: 1\n") == "line 1, column 3: mapping keys must be scalars");
  CHECK(errorOf("a: !!str 1\n") == "line 1, column 4: tags are not supported");
  CHECK(errorOf("a: &x 1\n") == "line 1, column 4: anchors and aliases are not supported");
  CHECK(errorOf("a: 1\n---\nb: 2\n") == "line 2, column 1: a file holds a single document");
}

// The classic "billion laughs" document: aliases are refused before anything is expanded.
TEST_CASE("yaml refuses alias bombs") {
  const std::string bomb = "a: &a [x, x, x, x, x, x, x, x, x]\n"
                           "b: &b [*a, *a, *a, *a, *a, *a, *a, *a, *a]\n"
                           "c: &c [*b, *b, *b, *b, *b, *b, *b, *b, *b]\n";
  CHECK(errorOf(bomb) == "line 1, column 4: anchors and aliases are not supported");
  CHECK(errorOf("a: *undefined\n").find("aliases are not supported") != std::string::npos);
}

TEST_CASE("yaml limits nesting and size") {
  const YamlLimits limits{.maxBytes = 1024, .maxDepth = 3};
  CHECK(parseYaml("[[[1]]]", limits).has_value());
  CHECK(errorOf("[[[[1]]]]", limits) == "line 1, column 4: nesting deeper than 3 levels");
  CHECK(errorOf("a:\n  b:\n    c:\n      d: 1\n", limits) ==
        "line 4, column 7: nesting deeper than 3 levels");
  CHECK(errorOf(std::string(1025, ' '), limits) ==
        "the text is 1025 bytes, more than the limit of 1024");

  // Far deeper than the default limit: refused early, without exhausting the stack.
  CHECK(errorOf(std::string(100'000, '[')).find("nesting deeper than 64 levels") !=
        std::string::npos);
}

TEST_CASE("yaml refuses to write text that is not UTF-8") {
  CHECK_FALSE(emitYaml(YamlNode::scalar("\xff")).has_value());
}
