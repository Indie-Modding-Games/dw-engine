#include <dwe/assets/map_document.hpp>

#include <doctest/doctest.h>

#include <numbers>
#include <string>

#include "place_command.hpp"
#include "state_trace.hpp"

namespace {

using dwe::app::Placement;
using dwe::app::placementFromText;
using dwe::app::TraceBattle;
using dwe::app::TraceCharacter;
using dwe::app::TraceFighter;
using dwe::app::TraceFrame;
using dwe::app::traceLine;

constexpr float Pi = std::numbers::pi_v<float>;

TEST_CASE("a trace line writes the characters in the original's axes and angles") {
  TraceFrame frame;
  frame.tick = 120;
  frame.seconds = 2.0;
  frame.buttons = 0x1000;
  frame.map = "twna01";
  // World (x, y, z) is the original's (x, -y, -z); a world yaw of -pi/2 is a quarter turn of the
  // original's.
  frame.player = TraceCharacter{.position = {256.0F, 0.0F, 1285.0F}, .yaw = -Pi / 2.0F, .id = {}};
  frame.partner = TraceCharacter{.position = {259.4F, -10.0F, 1084.6F}, .yaw = 0.0F, .id = {}};
  frame.npcs.push_back({.position = {-839.0F, 0.0F, -2210.0F}, .yaw = Pi, .id = 11});
  CHECK(traceLine(frame) ==
        R"({"src":"dwe","f":120,"time":2.0000,"in":4096,"map":"twna01","mode":"field",)"
        R"("player":{"x":256,"y":0,"z":-1285,"yaw":1024},)"
        R"("partner":{"x":259,"y":10,"z":-1085,"yaw":0},)"
        R"("npcs":[{"x":-839,"y":0,"z":2210,"yaw":2048,"id":11}]})");
}

TEST_CASE("a trace line in a fight lists the fighters, the partner first") {
  TraceFrame frame;
  frame.tick = 3;
  frame.seconds = 0.05;
  frame.map = "mayo00";
  TraceBattle battle;
  battle.phase = "fight";
  battle.frame = 7;
  battle.fighters.push_back({.x = 272,
                             .z = -3129,
                             .yaw = 0.0F,
                             .hp = 727,
                             .mp = 570,
                             .maxHp = 800,
                             .maxMp = 600,
                             .offense = 120,
                             .defense = 110,
                             .speed = 100,
                             .brain = 90,
                             .pose = "attacking"});
  frame.battle = battle;
  CHECK(traceLine(frame) ==
        R"({"src":"dwe","f":3,"time":0.0500,"in":0,"map":"mayo00","mode":"battle","npcs":[],)"
        R"("battle":{"phase":"fight","frame":7,"fighters":[)"
        R"({"x":272,"z":3129,"yaw":0,"hp":727,"mp":570,"max_hp":800,"max_mp":600,)"
        R"("off":120,"def":110,"spd":100,"brain":90,"pose":"attacking"}]}})");
}

TEST_CASE("place reads who, the original's ground point and its rotation") {
  const auto player = placementFromText("player 256 -1285 3700");
  REQUIRE(player.has_value());
  CHECK(player->who == Placement::Who::Player);
  CHECK(player->ground.x == doctest::Approx(256.0F));
  CHECK(player->ground.y == doctest::Approx(1285.0F));
  CHECK(dwe::assets::yawToOriginal(player->yaw) == 3700);

  const auto npc = placementFromText("npc:6  243 2565 0");
  REQUIRE(npc.has_value());
  CHECK(npc->who == Placement::Who::Npc);
  CHECK(npc->npc == 6);
  CHECK(npc->ground.y == doctest::Approx(-2565.0F));

  CHECK(placementFromText("partner 1 2").has_value() == false);
  CHECK(placementFromText("hero 1 2 3").has_value() == false);
  CHECK(placementFromText("npc:x 1 2 3").has_value() == false);
  CHECK(placementFromText("partner 1 2 3.5").has_value() == false);
}

} // namespace
