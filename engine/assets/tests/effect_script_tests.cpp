#include <dwe/assets/effect_script.hpp>
#include <dwe/core/yaml.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

using dwe::assets::EffectComparison;
using dwe::assets::EffectOpcode;
using dwe::assets::EffectOperator;
using dwe::assets::EffectScript;
using dwe::assets::EffectWidth;
using dwe::assets::readEffectScript;

namespace {

// A made-up technique in the shape dwx writes: a start script that spawns a sub-effect, plays a
// sound and hands the sub-effect its tick script, which ends it after a few frames.
constexpr std::string_view EffectYaml = R"(slot: 3
move: "made_up_breath"
header:
  start_script: "0x0040"
  sub_effect_count: 5
)";

constexpr std::string_view ScriptYaml = R"(start_script:
- label: "start"
  at: "0x0040"
  op: spawn
  count: 2
  table: "base+0x0010"
  stride: 8
  ordinal: 0
- at: "0x004c"
  op: push_address
  address: "base+0x005a"
- at: "0x0052"
  op: push_address
  address: "base+0x0074"
- at: "0x0058"
  op: sub
  sub: set_sub_effect_scripts
  args:
    tick_script: "l_005a"
    render_script: "l_0074"
- label: "l_005a"
  at: "0x005a"
  op: load_immediate
  value: -20
- at: "0x0060"
  op: branch_if
  variable: "instance+0x0000"
  width: 4
  comparison: less
  target: "l_0074"
- at: "0x006a"
  op: apply_operator
  variable: "base+0x0004"
  width: 2
  operator: shift_right
- at: "0x0070"
  op: push_immediate
  value: 15
- at: "0x0072"
  op: sub
  sub: play_sound
  args:
    sound: 15
- label: "l_0074"
  at: "0x0074"
  op: return
init_script:
- label: "init"
  at: "0x0076"
  op: stop
data:
- at: "0x0000"
  kind: variable
  width: 4
  value: 300
- at: "0x0004"
  kind: variable
  width: 2
  value: null
- at: "0x0006"
  kind: table
  width: 2
  size: 5
  values: [-2, 7]
  tail: "ab"
- at: "0x0010"
  kind: instances
  count: 2
  stride: 8
  free: true
- at: "0x0020"
  kind: vector
  size: 12
  values: [1, -1, 65536]
- at: "0x0030"
  kind: unused
  size: 2
  bytes: "beef"
)";

dwe::core::YamlNode parsed(std::string_view text) {
  auto node = dwe::core::parseYaml(text);
  REQUIRE_MESSAGE(node.has_value(), (node ? std::string{} : node.error().message));
  return *node;
}

std::int64_t readLittle(const EffectScript& script, std::size_t at, std::size_t size) {
  std::uint64_t bits = 0;
  for (std::size_t i = size; i-- > 0;) {
    bits = (bits << 8U) | static_cast<std::uint8_t>(script.data[at + i]);
  }
  const std::uint64_t sign = std::uint64_t{1} << ((size * 8) - 1);
  return static_cast<std::int64_t>((bits ^ sign) - sign);
}

} // namespace

TEST_CASE("an effect script reads with its labels resolved and its operands decoded") {
  const auto script = readEffectScript(parsed(EffectYaml), parsed(ScriptYaml));
  REQUIRE_MESSAGE(script.has_value(), (script ? std::string{} : script.error().message));
  CHECK(script->move == "made_up_breath");
  CHECK(script->subEffectCount == 5);
  CHECK(script->startScript == 0x40);
  CHECK(script->initScript == 0x76);
  REQUIRE(script->instructions.size() == 11);

  const auto& spawn = script->instructions[0];
  CHECK(spawn.opcode == EffectOpcode::Spawn);
  CHECK(spawn.count == 2);
  CHECK(spawn.target == 0x10);
  CHECK(spawn.stride == 8);

  const auto& address = script->instructions[1];
  CHECK(address.opcode == EffectOpcode::PushAddress);
  CHECK(address.variable.base);
  CHECK(address.variable.offset == 0x5a);

  const auto& scripts = script->instructions[3];
  CHECK(scripts.sub == "set_sub_effect_scripts");
  CHECK(scripts.argCount == 2);

  CHECK(script->instructions[4].value == -20);
  const auto& branch = script->instructions[5];
  CHECK(branch.opcode == EffectOpcode::BranchIf);
  CHECK_FALSE(branch.variable.base);
  CHECK(branch.comparison == EffectComparison::Less);
  CHECK(branch.target == 0x74);
  const auto& shift = script->instructions[6];
  CHECK(shift.op == EffectOperator::ShiftRight);
  CHECK(shift.width == EffectWidth::Int16);
  CHECK(script->instructions[8].sub == "play_sound");
  CHECK(script->instructions[8].argCount == 1);
  CHECK(script->instructions[9].opcode == EffectOpcode::Return);

  CHECK(script->instructionAt(0x60) == 5);
  CHECK_FALSE(script->instructionAt(0x61).has_value());
}

TEST_CASE("an effect script's data area holds the values the scripts start from") {
  const auto script = readEffectScript(parsed(EffectYaml), parsed(ScriptYaml));
  REQUIRE(script.has_value());
  REQUIRE(script->data.size() == 0x40);
  CHECK(readLittle(*script, 0x00, 4) == 300);
  CHECK(static_cast<std::uint8_t>(script->data[0x04]) == 0xCD); // null: fill
  CHECK(readLittle(*script, 0x06, 2) == -2);
  CHECK(readLittle(*script, 0x08, 2) == 7);
  CHECK(static_cast<std::uint8_t>(script->data[0x0a]) == 0xab);
  CHECK(readLittle(*script, 0x10, 4) == -1); // free instances start at frame -1
  CHECK(readLittle(*script, 0x18, 4) == -1);
  CHECK(static_cast<std::uint8_t>(script->data[0x14]) == 0xCD);
  CHECK(readLittle(*script, 0x24, 4) == -1);
  CHECK(readLittle(*script, 0x28, 4) == 65536);
  CHECK(static_cast<std::uint8_t>(script->data[0x31]) == 0xef);
}

TEST_CASE("an effect script with an op the game does not have, or data past its area, is refused") {
  const std::string unknownOp{
      std::string{ScriptYaml}.replace(std::string{ScriptYaml}.find("op: stop"), 8, "op: teleport")};
  const auto script = readEffectScript(parsed(EffectYaml), parsed(unknownOp));
  REQUIRE_FALSE(script.has_value());
  CHECK(script.error().message.find("teleport") != std::string::npos);

  const std::string pastData{
      std::string{ScriptYaml}.replace(std::string{ScriptYaml}.find("\"0x0030\""), 8, "\"0x003f\"")};
  CHECK_FALSE(readEffectScript(parsed(EffectYaml), parsed(pastData)).has_value());
}
