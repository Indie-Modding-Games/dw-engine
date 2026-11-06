#include <dwe/assets/file_system.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/collision_triggers.hpp>
#include <dwe/game/field/script_warps.hpp>
#include <dwe/game/field/trigger_bands.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using dwe::game::field::parseTriggerBands;
using dwe::game::field::TriggerBand;
using dwe::game::field::TriggerBands;
using dwe::game::field::WarpVia;

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

constexpr std::string_view Original = R"(step:
  first: 51
  last: 79
confirm:
  first: 80
  last: 109
exits:
  first: 110
  last: 119
toilet: 120
blocked_bit: 128
)";

// The original's document with one line replaced.
std::string with(std::string_view line, std::string_view replacement) {
  std::string text{Original};
  const std::size_t at = text.find(line);
  REQUIRE(at != std::string::npos);
  text.replace(at, line.size(), replacement);
  return text;
}

} // namespace

TEST_CASE("the default bands are the original's (tamer.c: checkMapInteraction)") {
  const TriggerBands bands;
  CHECK(bands.step == TriggerBand{.first = 51, .last = 79});
  CHECK(bands.confirm == TriggerBand{.first = 80, .last = 109});
  CHECK(bands.exits == TriggerBand{.first = 110, .last = 119});
  CHECK(bands.toilet == 120);
  CHECK(bands.blockedBit == 0x80);
  CHECK(bands.blockedBit == dwe::assets::MapCollision::BlockedBit);
  CHECK(bands.exits.first == dwe::core::collision::FirstWarpTrigger);
  CHECK(bands.exits.last - bands.exits.first + 1 == dwe::core::collision::WarpTriggerCount);
}

TEST_CASE("the game pack's field/triggers keeps the original's bands") {
  const dwe::assets::DiskFileSystem disk;
  const auto bytes = disk.readFile(std::filesystem::path{DWE_GAME_PACK} / "field/triggers.yaml");
  REQUIRE(bytes.has_value());
  const auto bands = parseTriggerBands(
      std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  REQUIRE_MESSAGE(bands.has_value(), errorOf(bands));
  CHECK(*bands == TriggerBands{});
  // Every value from 0 to 127 means what it meant before the bands were data.
  for (std::int32_t value = 0; value < 0x80; ++value) {
    CAPTURE(value);
    WarpVia via = WarpVia::Other;
    if (value >= 51 && value <= 79) {
      via = WarpVia::Step;
    } else if (value >= 80 && value <= 109) {
      via = WarpVia::Confirm;
    }
    CHECK(dwe::game::field::warpViaOf(value, *bands) == via);
    CHECK(bands->exitOf(value) ==
          (value >= 110 && value <= 119 ? std::optional<std::int32_t>{value - 110} : std::nullopt));
  }
}

TEST_CASE("the bands read from a document move the cells' meaning") {
  const auto bands =
      parseTriggerBands(with("  first: 110\n  last: 119\n", "  first: 20\n  last: 24\n"));
  REQUIRE_MESSAGE(bands.has_value(), errorOf(bands));
  CHECK(bands->exitOf(22) == std::optional<std::int32_t>{2});
  CHECK_FALSE(bands->exitOf(110).has_value());
  CHECK(bands->exitValue(4) == 24);
  CHECK(dwe::game::field::warpViaOf(60, *bands) == WarpVia::Step);
}

TEST_CASE("field/triggers refuses bands that are no trigger cells or overlap") {
  CHECK(errorOf(parseTriggerBands(with("first: 51\n  last: 79", "first: 79\n  last: 51")))
            .contains("step: first is past last"));
  CHECK(errorOf(parseTriggerBands(with("first: 80\n  last: 109", "first: 70\n  last: 109")))
            .contains("step and confirm share values"));
  CHECK(errorOf(parseTriggerBands(with("toilet: 120", "toilet: 115")))
            .contains("exits and toilet share values"));
  CHECK(errorOf(parseTriggerBands(with("toilet: 120", "toilet: 128")))
            .contains("toilet: the values must be 1 to 127"));
  CHECK(errorOf(parseTriggerBands(with("first: 51", "first: 0")))
            .contains("step: the values must be 1 to 127"));
  CHECK(errorOf(parseTriggerBands(with("last: 119\ntoilet: 120", "last: 121\ntoilet: 122")))
            .contains("exits: a map has at most 10 exits"));
  CHECK(errorOf(parseTriggerBands(with("blocked_bit: 128", "blocked_bit: 64")))
            .contains("blocked_bit is fixed by the map format: it must be 128"));
  CHECK_FALSE(parseTriggerBands(with("toilet: 120", "toilet: 120\nwater: 121")).has_value());
  CHECK_FALSE(parseTriggerBands(with("toilet: 120\n", "")).has_value());
}

TEST_CASE("without field/triggers the field takes the original's bands") {
  dwe::assets::MemoryFileSystem memory;
  memory.addFile("game/pack.yaml", "id: game\nversion: 0.1.0\nlayer: base\n");
  const std::vector<std::filesystem::path> packs{"game"};
  const auto vfs = dwe::assets::VirtualFileSystem::mount(memory, packs);
  REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
  const auto bands = dwe::game::field::loadTriggerBands(*vfs);
  REQUIRE_MESSAGE(bands.has_value(), errorOf(bands));
  CHECK(*bands == TriggerBands{});
}
