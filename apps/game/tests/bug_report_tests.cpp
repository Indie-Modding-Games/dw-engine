#include <dwe/core/yaml.hpp>

#include <doctest/doctest.h>

#include <ctime>
#include <initializer_list>
#include <string>
#include <string_view>

#include "bug_report.hpp"

namespace {

using dwe::app::BugReportState;
using dwe::app::ReportFighter;
using dwe::app::ReportNpc;
using dwe::app::ReportSaveKind;
using dwe::core::YamlNode;

std::tm someTime() {
  std::tm local{};
  local.tm_year = 2026 - 1900;
  local.tm_mon = 9;
  local.tm_mday = 29;
  local.tm_hour = 8;
  local.tm_min = 5;
  local.tm_sec = 3;
  return local;
}

// The text of a scalar under a path of mapping keys, or "" when it is missing.
std::string at(const YamlNode& root, std::initializer_list<std::string_view> path) {
  const YamlNode* node = &root;
  for (const std::string_view key : path) {
    node = node->isMapping() ? node->find(key) : nullptr;
    if (node == nullptr) {
      return {};
    }
  }
  return node->isScalar() ? node->text() : std::string{};
}

BugReportState battleState() {
  BugReportState state;
  state.commit = "4027c88c-dirty";
  state.gameVersion = "0.1.0";
  state.created = "2026-10-29 08:05:03";
  state.map = "mayo03";
  state.time = {.year = 0, .day = 3, .hour = 14, .minute = 30};
  state.screen = "battle";
  state.battlePhase = "fight";
  state.scriptRunning = true;
  state.scriptSections = {"script 3 section 1"};
  state.player = {.position = {10.0F, 0.0F, -20.5F}, .yaw = 1.5F};
  ReportNpc npc;
  npc.scriptId = 7;
  npc.slot = 2;
  npc.species = "45";
  npc.model = "npc/45/model";
  npc.held = true;
  state.npcs.push_back(npc);
  ReportFighter partner;
  partner.species = "1";
  partner.hp = 80;
  partner.maxHp = 100;
  state.fighters.push_back(partner);
  ReportFighter enemy;
  enemy.species = "45";
  enemy.npc = 7;
  enemy.hp = 0;
  state.fighters.push_back(enemy);
  state.partnerSpecies = "1";
  state.partnerStatus = {{.name = "partner.name", .value = "Mi: compañero \"1\""}};
  state.overlaps = 2;
  state.save = {
      .kind = ReportSaveKind::FieldSnapshot, .secondsAgo = 3.25, .reason = "cannot save: battle"};
  return state;
}

} // namespace

TEST_CASE("bug report: the folder is named by the local time and the map") {
  CHECK(dwe::app::bugReportFolderName(someTime(), "mayo03") == "20261029-080503-mayo03");
}

TEST_CASE("bug report: a map id that is no file name is made one") {
  CHECK(dwe::app::bugReportFolderName(someTime(), "a/b c") == "20261029-080503-a_b_c");
  CHECK(dwe::app::bugReportFolderName(someTime(), "") == "20261029-080503-nomap");
}

TEST_CASE("bug report: state.yaml says what the game was doing") {
  const auto text = dwe::app::bugReportStateYaml(battleState());
  REQUIRE(text.has_value());
  const auto root = dwe::core::parseYaml(*text);
  REQUIRE(root.has_value());
  CHECK(at(*root, {"build", "commit"}) == "4027c88c-dirty");
  CHECK(at(*root, {"map"}) == "mayo03");
  CHECK(at(*root, {"game_time", "day"}) == "3");
  CHECK(at(*root, {"screen", "screen"}) == "battle");
  CHECK(at(*root, {"screen", "battle_phase"}) == "fight");
  CHECK(at(*root, {"screen", "script_running"}) == "true");
  CHECK(at(*root, {"player", "z"}) == "-20.50");
  CHECK(at(*root, {"player", "yaw"}) == "1.50");
  CHECK(at(*root, {"save", "kind"}) == "field_snapshot");
  CHECK(at(*root, {"save", "file"}) == "save.yaml");
  CHECK(at(*root, {"save", "seconds_ago"}) == "3.25");
  CHECK(at(*root, {"field", "overlaps"}) == "2");
  CHECK(at(*root, {"partner_stats", "status", "partner.name"}) == "Mi: compañero \"1\"");

  const YamlNode* npcs = root->find("npcs");
  REQUIRE(npcs != nullptr);
  REQUIRE(npcs->items().size() == 1);
  CHECK(at(npcs->items()[0], {"script_id"}) == "7");
  CHECK(at(npcs->items()[0], {"model"}) == "npc/45/model");
  CHECK(at(npcs->items()[0], {"held"}) == "true");

  const YamlNode* fighters = root->find("battle_fighters");
  REQUIRE(fighters != nullptr);
  REQUIRE(fighters->items().size() == 2);
  CHECK(at(fighters->items()[0], {"npc"}).empty());
  CHECK(at(fighters->items()[0], {"hp"}) == "80");
  CHECK(at(fighters->items()[1], {"npc"}) == "7");
}

TEST_CASE("bug report: without a save, state.yaml names no file") {
  BugReportState state = battleState();
  state.save = {.kind = ReportSaveKind::None, .secondsAgo = 0.0, .reason = "no save yet"};
  const auto root = dwe::core::parseYaml(*dwe::app::bugReportStateYaml(state));
  REQUIRE(root.has_value());
  CHECK(at(*root, {"save", "kind"}) == "none");
  CHECK(at(*root, {"save", "file"}).empty());
  CHECK(at(*root, {"save", "reason"}) == "no save yet");
}

TEST_CASE("bug report: the route loads the save and waits on its map") {
  const auto text = dwe::app::bugReportRouteYaml("mayo03", true);
  REQUIRE(text.has_value());
  const auto root = dwe::core::parseYaml(*text);
  REQUIRE(root.has_value());
  CHECK(at(*root, {"load_save"}) == "save.yaml");
  CHECK(at(*root, {"device"}) == "keyboard");
  const YamlNode* steps = root->find("steps");
  REQUIRE(steps != nullptr);
  REQUIRE(steps->items().size() == 2);
  CHECK(at(steps->items()[0], {"wait", "map"}) == "mayo03");
  CHECK(at(steps->items()[1], {"screenshot"}) == "report");
}

TEST_CASE("bug report: a route with no save starts a new game") {
  const auto root = dwe::core::parseYaml(*dwe::app::bugReportRouteYaml("mayo03", false));
  REQUIRE(root.has_value());
  CHECK(root->find("load_save") == nullptr);
}
