#include <dwe/core/yaml.hpp>

#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <utility>

#include "yaml_edits.hpp"

// The map editor compares what it writes with the engine's documents byte for byte, so the layout of emitYaml is
// contract (docs/architecture/herramientas-externas.md). This test pins it for every kind of value those documents
// hold. If it fails, the change breaks the editor: it is a contract change, not a test to update.

using dwe::core::YamlNode;
using dwe::dwx::testing::emitted;
using dwe::dwx::testing::savedUnchanged;

namespace {

YamlNode scalar(std::string text) {
    return YamlNode::scalar(std::move(text));
}

YamlNode quoted(std::string text) {
    return YamlNode::scalar(std::move(text), YamlNode::ScalarStyle::Quoted);
}

YamlNode point(std::string x, std::string y) {
    YamlNode node = YamlNode::mapping();
    node.set("x", scalar(std::move(x)));
    node.set("y", scalar(std::move(y)));
    return node;
}

// One value of each kind that maps, the map table and script listings hold, in the shapes they nest in.
YamlNode everyKindOfValue() {
    YamlNode root = YamlNode::mapping();
    root.set("code_start", scalar("0x002c"));
    root.set("negative", scalar("-3715"));
    root.set("none", scalar("null"));
    root.set("flag", scalar("false"));
    root.set("id", quoted("mayo01"));
    root.set("number_text", quoted("0 1 2"));
    root.set("empty_text", quoted(""));
    root.set("padded_text", quoted("   File City  "));
    root.set("text", quoted("I'm \"here\"!\nNext\\line"));
    root.set("unicode", quoted("ñandú ☃"));
    root.set("collision_row", scalar("..##ABa1"));
    root.set("empty_list", YamlNode::sequence());
    root.set("empty_mapping", YamlNode::mapping());
    root.set("spawn", point("440", "-1"));
    YamlNode numbers = YamlNode::sequence();
    numbers.append(scalar("-1"));
    numbers.append(scalar("0"));
    root.set("numbers", std::move(numbers));
    YamlNode instruction = YamlNode::mapping();
    instruction.set("at", scalar("0x0030"));
    instruction.set("label", scalar("l_0030"));
    YamlNode conditions = YamlNode::sequence();
    YamlNode condition = YamlNode::mapping();
    condition.set("test", scalar("pstat"));
    condition.set("position", point("1", "2"));
    conditions.append(std::move(condition));
    instruction.set("conditions", std::move(conditions));
    YamlNode cases = YamlNode::sequence();
    cases.append(scalar("l_0042"));
    instruction.set("cases", std::move(cases));
    YamlNode instructions = YamlNode::sequence();
    instructions.append(std::move(instruction));
    YamlNode rows = YamlNode::sequence();
    rows.append(scalar("1"));
    instructions.append(std::move(rows));
    root.set("instructions", std::move(instructions));
    return root;
}

constexpr std::string_view EveryKindOfValueText = R"(code_start: 0x002c
negative: -3715
none: null
flag: false
id: "mayo01"
number_text: "0 1 2"
empty_text: ""
padded_text: "   File City  "
text: "I'm \"here\"!\nNext\\line"
unicode: "ñandú ☃"
collision_row: ..##ABa1
empty_list: []
empty_mapping: {}
spawn:
  x: 440
  y: -1
numbers:
- -1
- 0
instructions:
- at: 0x0030
  label: l_0030
  conditions:
  - test: pstat
    position:
      x: 1
      y: 2
  cases:
  - l_0042
- - 1
)";

} // namespace

TEST_CASE("the layout of emitYaml that the editor's byte comparison relies on does not change") {
    CHECK(emitted(everyKindOfValue()) == EveryKindOfValueText);
    CHECK(savedUnchanged(EveryKindOfValueText) == EveryKindOfValueText);
}

TEST_CASE("emitYaml does not wrap long lines, so a long text or collision row stays on one line") {
    const std::string longText(300, 'a');
    YamlNode root = YamlNode::mapping();
    root.set("row", scalar(longText));
    root.set("text", quoted(longText + " " + longText));
    CHECK(emitted(root) == "row: " + longText + "\ntext: \"" + longText + " " + longText + "\"\n");
}

TEST_CASE("a plain value that needs quotes is written in single quotes, and in double quotes once it is read back") {
    // The engine's writers quote every text, so no extracted document has one; a tool that writes such a value plain
    // gets a document that changes on its first open and save, and is stable after that.
    YamlNode root = YamlNode::mapping();
    root.set("plain_with_colon", scalar("a: b"));
    CHECK(emitted(root) == "plain_with_colon: 'a: b'\n");
    CHECK(savedUnchanged(emitted(root)) == "plain_with_colon: \"a: b\"\n");
    CHECK(savedUnchanged(savedUnchanged(emitted(root))) == savedUnchanged(emitted(root)));
}
