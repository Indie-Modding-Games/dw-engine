#include <dwe/assets/file_system.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/game/field/rules.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {

template <typename T>
std::string errorOf(const dwe::core::Result<T>& result) {
  return result ? std::string{} : result.error().message;
}

constexpr std::string_view Rules = R"(player:
  model: digimon/species_g/model
  radius: 100
  walk_speed: 400
  run_speed: 1000
  turn_speed: 0
partner:
  model: digimon/species_a/model
  radius: 120
  walk_speed: 452.5
  run_speed: 870
  turn_speed: 12.5
  run_distance: 3.5
  walk_distance: 2.5
  spawn_distance: 200
  trail_length: 30
  sight_half_width: 200
  stuck_seconds: 1.5
  teleport_distance: 2000
  happy_gesture_from: 31
  unhappy_gesture_below: -30
  gesture_idle_plays: 5
input:
  dead_zone: 0.2
  arrive_distance: 25
transition:
  frame_seconds: 0.05
  fade_out_frames: 20
  fade_in_frames: 10
  shade_step: 7
  shade_max: 160
  shade_full: 128
  name_frame: 10
time_of_day:
  day_start: 6
  dusk_start: 16
  night_start: 20
  dusk_light: [1.0, 0.7, 0.7]
  night_light: [0.5, 0.5, 0.5]
)";

std::string replaced(std::string_view text, std::string_view from, std::string_view to) {
  std::string result{text};
  result.replace(result.find(from), from.size(), to);
  return result;
}

} // namespace

TEST_CASE("field rules read every tunable") {
  const auto rules = dwe::game::field::parseFieldRules(Rules);
  REQUIRE_MESSAGE(rules.has_value(), errorOf(rules));
  CHECK(rules->player.model == "digimon/species_g/model");
  CHECK(rules->player.runSpeed == doctest::Approx(1000.0F));
  CHECK(rules->player.turnSpeed == doctest::Approx(0.0F));
  CHECK(rules->partner.mover.walkSpeed == doctest::Approx(452.5F));
  CHECK(rules->partner.mover.turnSpeed == doctest::Approx(12.5F));
  CHECK(rules->partner.trailLength == 30);
  CHECK(rules->partner.stuckSeconds == doctest::Approx(1.5F));
  CHECK(rules->partner.happyGestureFrom == 31);
  CHECK(rules->partner.unhappyGestureBelow == -30);
  CHECK(rules->partner.gestureIdlePlays == 5);
  CHECK(rules->input.deadZone == doctest::Approx(0.2F));
  CHECK(rules->transition.frameSeconds == doctest::Approx(0.05F));
  CHECK(rules->transition.fadeInFrames == 10);
  CHECK(rules->transition.shadeFull == 128);
  CHECK(rules->transition.nameFrame == 10);
  CHECK(rules->timeOfDay.duskStart == 16);
  CHECK(rules->timeOfDay.duskLight.g == doctest::Approx(0.7F));
  CHECK(rules->timeOfDay.nightLight.r == doctest::Approx(0.5F));
}

TEST_CASE("broken field rules are refused") {
  CHECK_FALSE(
      dwe::game::field::parseFieldRules(replaced(Rules, "radius: 100", "radius: 0")).has_value());
  CHECK_FALSE(
      dwe::game::field::parseFieldRules(replaced(Rules, "run_speed: 1000", "run_speed: 300"))
          .has_value());
  CHECK_FALSE(
      dwe::game::field::parseFieldRules(replaced(Rules, "walk_distance: 2.5", "walk_distance: 4"))
          .has_value());
  CHECK_FALSE(dwe::game::field::parseFieldRules(
                  replaced(Rules, "gesture_idle_plays: 5", "gesture_idle_plays: 0"))
                  .has_value());
  CHECK_FALSE(dwe::game::field::parseFieldRules(
                  replaced(Rules, "happy_gesture_from: 31", "happy_gesture_from: -40"))
                  .has_value());
  CHECK_FALSE(dwe::game::field::parseFieldRules(replaced(Rules, "dead_zone: 0.2", "dead_zone: 1"))
                  .has_value());
  CHECK_FALSE(dwe::game::field::parseFieldRules(replaced(Rules, "turn_speed: 0", "turn_speed: -1"))
                  .has_value());
  CHECK_FALSE(dwe::game::field::parseFieldRules(replaced(Rules, "dusk_start: 16", "dusk_start: 21"))
                  .has_value());
  CHECK_FALSE(dwe::game::field::parseFieldRules(replaced(Rules, "shade_step: 7", "shade_step: 0"))
                  .has_value());
  CHECK_FALSE(dwe::game::field::parseFieldRules(replaced(Rules, "shade_max: 160", "shade_max: 300"))
                  .has_value());
  CHECK_FALSE(
      dwe::game::field::parseFieldRules(replaced(Rules, "frame_seconds: 0.05", "frame_seconds: 0"))
          .has_value());
  CHECK_FALSE(dwe::game::field::parseFieldRules(
                  replaced(Rules, "night_light: [0.5, 0.5, 0.5]", "night_light: [0.5, 2]"))
                  .has_value());
  const auto unknown = dwe::game::field::parseFieldRules(
      replaced(Rules, "  radius: 120", "  radius: 120\n  size: 2"));
  REQUIRE_FALSE(unknown.has_value());
  CHECK(unknown.error().message.find("'size'") != std::string::npos);
  const auto missing =
      dwe::game::field::parseFieldRules(replaced(Rules, "  trail_length: 30\n", ""));
  REQUIRE_FALSE(missing.has_value());
  CHECK(missing.error().message.find("trail_length") != std::string::npos);
}

TEST_CASE("the fade's shades follow the original's curve") {
  // fade.c: renderFadeOut and renderFadeIn with fadeToBlack(20) and fadeFromBlack(20): a step of
  // 160 / 21 = 7.
  const dwe::game::field::TransitionRules rules;
  using dwe::game::field::fadeInShade;
  using dwe::game::field::fadeOutShade;
  using dwe::game::field::shadeDarkness;
  // The fade in falls from 160 by 7 per frame and ends at 20 on its 20th frame.
  CHECK(fadeInShade(rules, 1) == 153);
  CHECK(fadeInShade(rules, 10) == 90);
  CHECK(fadeInShade(rules, 20) == 20);
  CHECK(fadeInShade(rules, 30) == 0);
  // The fade out starts from those 20: 27 on its first frame, 7 more per frame, at most 160.
  CHECK(fadeOutShade(rules, 1) == 27);
  CHECK(fadeOutShade(rules, 10) == 90);
  CHECK(fadeOutShade(rules, 19) == 153);
  CHECK(fadeOutShade(rules, 30) == 160);
  for (std::int32_t frame = 1; frame < rules.fadeOutFrames; ++frame) {
    CHECK(fadeOutShade(rules, frame + 1) - fadeOutShade(rules, frame) == 7);
  }
  // The subtracted grey is the white texel times shade / 128: black from 128 on (the fade out's
  // 16th frame).
  CHECK(shadeDarkness(rules, 0) == doctest::Approx(0.0F));
  CHECK(shadeDarkness(rules, 64) == doctest::Approx(0.5F));
  CHECK(shadeDarkness(rules, fadeOutShade(rules, 15)) < 1.0F);
  CHECK(shadeDarkness(rules, fadeOutShade(rules, 16)) == doctest::Approx(1.0F));
  CHECK(shadeDarkness(rules, 160) == doctest::Approx(1.0F));
}

TEST_CASE("field warps read the start, and the old scripted warps are refused") {
  const auto warps = dwe::game::field::parseFieldWarps("start:\n  map: twna01\n  exit: 1\n");
  REQUIRE_MESSAGE(warps.has_value(), errorOf(warps));
  CHECK(warps->startMap == "twna01");
  CHECK(warps->startExit == 1);
  // The scripts' warps moved to field/script_warps (docs/architecture/warps-de-script.md).
  CHECK_FALSE(dwe::game::field::parseFieldWarps("start:\n  map: a\n  exit: 0\nscripted_warps: []\n")
                  .has_value());
}

TEST_CASE("the game pack's field data is valid") {
  const dwe::assets::DiskFileSystem disk;
  // The game pack depends on base; a stand-in manifest lets it mount alone.
  dwe::assets::MemoryFileSystem memory;
  const auto files = dwe::assets::listPackFiles(disk, DWE_GAME_PACK);
  REQUIRE_MESSAGE(files.has_value(), errorOf(files));
  memory.addFile("base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\n");
  const auto manifest = disk.readFile(std::filesystem::path{DWE_GAME_PACK} / "pack.yaml");
  REQUIRE(manifest.has_value());
  memory.addFile("game/pack.yaml", *manifest);
  for (const auto& file : *files) {
    const auto bytes = disk.readFile(std::filesystem::path{DWE_GAME_PACK} / file.path);
    REQUIRE(bytes.has_value());
    memory.addFile("game/" + file.path, *bytes);
  }
  const std::vector<std::filesystem::path> packs{"base", "game"};
  const auto vfs = dwe::assets::VirtualFileSystem::mount(memory, packs);
  REQUIRE_MESSAGE(vfs.has_value(), errorOf(vfs));
  const auto rules = dwe::game::field::loadFieldRules(*vfs);
  CHECK_MESSAGE(rules.has_value(), errorOf(rules));
  // field/triggers keeps the original's bands (trigger_bands_tests.cpp checks them one by one).
  CHECK((rules && rules->triggers == dwe::game::field::TriggerBands{}));
  const auto warps = dwe::game::field::loadFieldWarps(*vfs);
  CHECK_MESSAGE(warps.has_value(), errorOf(warps));
}
