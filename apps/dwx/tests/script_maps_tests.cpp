#include <dwe/formats/script/disassembly.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

#include "script_maps.hpp"

using namespace dwe::dwx;
using dwe::formats::script::Argument;
using dwe::formats::script::ArgumentKind;
using dwe::formats::script::Instruction;

namespace {

constexpr std::uint8_t JumpToFileAndLink = 0x14;
constexpr std::uint8_t JumpToFile = 0x17;
constexpr std::uint8_t CallRoutine = 0x64;
constexpr std::uint8_t SetScript = 0xFB;

Instruction instruction(std::uint8_t opcode,
                        std::initializer_list<std::pair<std::string_view, std::int32_t>> values) {
  Instruction result;
  result.opcode = opcode;
  for (const auto& [name, value] : values) {
    result.arguments.push_back(Argument{.name = name, .kind = ArgumentKind::U16, .value = value});
  }
  return result;
}

ScriptCode script(std::uint16_t id, std::vector<Instruction> instructions) {
  ScriptCode code{.id = id, .code = {}};
  code.code.instructions = std::move(instructions);
  return code;
}

} // namespace

TEST_CASE("a routine runs on the maps that load its script or a script that jumps into it") {
  const std::vector<ScriptCode> scripts{
      // Script 0 loads script 1 on map 10 and script 2 on maps 11 and 12.
      script(0, {instruction(SetScript, {{"script", 1}, {"map", 10}}),
                 instruction(SetScript, {{"script", 2}, {"map", 12}}),
                 instruction(SetScript, {{"script", 2}, {"map", 11}})}),
      script(1, {instruction(CallRoutine, {{"routine", 0x2B}})}),
      // Script 2 runs script 3's code; script 3 runs script 4's.
      script(2, {instruction(JumpToFile, {{"script", 3}, {"section", 0}})}),
      script(3, {instruction(JumpToFileAndLink, {{"script", 4}, {"section", 1}})}),
      script(4, {instruction(CallRoutine, {{"routine", 0x2B}}),
                 instruction(CallRoutine, {{"routine", 0x31}})}),
      // Nothing loads script 5.
      script(5, {instruction(CallRoutine, {{"routine", 0x31}})}),
  };
  const RoutineMaps boxes = findRoutineMaps(scripts, 0x2B);
  CHECK(boxes.maps == std::vector<std::uint16_t>{10, 11, 12});
  CHECK(boxes.scriptsWithoutMap.empty());

  const RoutineMaps pedestal = findRoutineMaps(scripts, 0x31);
  CHECK(pedestal.maps == std::vector<std::uint16_t>{11, 12});
  CHECK(pedestal.scriptsWithoutMap == std::vector<std::uint16_t>{5});

  const RoutineMaps none = findRoutineMaps(scripts, 0x10);
  CHECK(none.maps.empty());
  CHECK(none.scriptsWithoutMap.empty());
}
