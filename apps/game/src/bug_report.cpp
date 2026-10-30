#include "bug_report.hpp"

#include <dwe/core/yaml.hpp>

#include <cctype>
#include <format>
#include <string>
#include <utility>

namespace dwe::app {
namespace {

using core::YamlNode;

// How long the report's route waits for the save's map before it fails, and then on it.
constexpr int RouteMapTimeout = 60;
constexpr int RouteSettleSeconds = 2;
constexpr int RouteTimeout = 120;

YamlNode text(std::string value) {
  return YamlNode::scalar(std::move(value), YamlNode::ScalarStyle::Quoted);
}

YamlNode number(std::int64_t value) {
  return YamlNode::scalar(std::to_string(value));
}

YamlNode decimal(double value) {
  return YamlNode::scalar(std::format("{:.2f}", value));
}

YamlNode flag(bool value) {
  return YamlNode::scalar(value ? "true" : "false");
}

YamlNode poseNode(const ReportPose& pose) {
  YamlNode node = YamlNode::mapping();
  node.set("x", decimal(pose.position.x));
  node.set("y", decimal(pose.position.y));
  node.set("z", decimal(pose.position.z));
  node.set("yaw", decimal(pose.yaw));
  return node;
}

std::string_view saveKindName(ReportSaveKind kind) {
  switch (kind) {
  case ReportSaveKind::Now:
    return "now";
  case ReportSaveKind::FieldSnapshot:
    return "field_snapshot";
  case ReportSaveKind::None:
    return "none";
  }
  return "none";
}

YamlNode saveNode(const ReportSave& save) {
  YamlNode node = YamlNode::mapping();
  node.set("kind", YamlNode::scalar(std::string{saveKindName(save.kind)}));
  if (save.kind != ReportSaveKind::None) {
    node.set("file", text(std::string{ReportSaveFile}));
  }
  if (save.kind == ReportSaveKind::FieldSnapshot) {
    node.set("seconds_ago", decimal(save.secondsAgo));
  }
  if (!save.reason.empty()) {
    node.set("reason", text(save.reason));
  }
  return node;
}

YamlNode screenNode(const BugReportState& state) {
  YamlNode node = YamlNode::mapping();
  node.set("screen", text(state.screen));
  if (!state.battlePhase.empty()) {
    node.set("battle_phase", text(state.battlePhase));
  }
  node.set("script_running", flag(state.scriptRunning));
  YamlNode sections = YamlNode::sequence();
  for (const std::string& section : state.scriptSections) {
    sections.append(text(section));
  }
  node.set("script_sections", std::move(sections));
  node.set("held_by_script", flag(state.heldByScript));
  node.set("held_for_care", flag(state.heldForCare));
  node.set("dialog_open", flag(state.dialogOpen));
  node.set("menu_open", flag(state.menuOpen));
  node.set("map_changing", flag(state.mapChanging));
  return node;
}

YamlNode npcNode(const ReportNpc& npc) {
  YamlNode node = YamlNode::mapping();
  node.set("script_id", number(npc.scriptId));
  node.set("slot", number(static_cast<std::int64_t>(npc.slot)));
  node.set("species", text(npc.species));
  node.set("model", text(npc.model));
  node.set("pose", poseNode(npc.pose));
  node.set("held", flag(npc.held));
  node.set("scripted", flag(npc.scripted));
  node.set("scripted_animation", number(npc.scriptedAnimation));
  return node;
}

YamlNode fighterNode(const ReportFighter& fighter) {
  YamlNode node = YamlNode::mapping();
  node.set("species", text(fighter.species));
  if (fighter.npc) {
    node.set("npc", number(*fighter.npc));
  }
  node.set("hp", number(fighter.hp));
  node.set("max_hp", number(fighter.maxHp));
  node.set("mp", number(fighter.mp));
  node.set("max_mp", number(fighter.maxMp));
  node.set("x", number(fighter.x));
  node.set("z", number(fighter.z));
  return node;
}

} // namespace

std::string bugReportFolderName(const std::tm& local, std::string_view map) {
  std::string safe;
  for (const char c : map) {
    const bool kept = std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_';
    safe += kept ? c : '_';
  }
  if (safe.empty()) {
    safe = "nomap";
  }
  return std::format("{:04}{:02}{:02}-{:02}{:02}{:02}-{}", local.tm_year + 1900, local.tm_mon + 1,
                     local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, safe);
}

core::Result<std::string> bugReportStateYaml(const BugReportState& state) {
  YamlNode root = YamlNode::mapping();
  YamlNode build = YamlNode::mapping();
  build.set("commit", text(state.commit));
  build.set("game_version", text(state.gameVersion));
  root.set("build", std::move(build));
  root.set("created", text(state.created));
  root.set("map", text(state.map));
  YamlNode time = YamlNode::mapping();
  time.set("year", number(state.time.year));
  time.set("day", number(state.time.day));
  time.set("hour", number(state.time.hour));
  time.set("minute", number(state.time.minute));
  root.set("game_time", std::move(time));
  root.set("screen", screenNode(state));
  root.set("save", saveNode(state.save));
  root.set("player", poseNode(state.player));
  root.set("partner", poseNode(state.partner));
  YamlNode npcs = YamlNode::sequence();
  for (const ReportNpc& npc : state.npcs) {
    npcs.append(npcNode(npc));
  }
  root.set("npcs", std::move(npcs));
  YamlNode fighters = YamlNode::sequence();
  for (const ReportFighter& fighter : state.fighters) {
    fighters.append(fighterNode(fighter));
  }
  root.set("battle_fighters", std::move(fighters));
  YamlNode partnerStats = YamlNode::mapping();
  partnerStats.set("species", text(state.partnerSpecies));
  YamlNode status = YamlNode::mapping();
  for (const ReportSetting& setting : state.partnerStatus) {
    status.set(setting.name, text(setting.value));
  }
  partnerStats.set("status", std::move(status));
  root.set("partner_stats", std::move(partnerStats));
  YamlNode field = YamlNode::mapping();
  field.set("overlaps", number(state.overlaps));
  root.set("field", std::move(field));
  return core::emitYaml(root);
}

core::Result<std::string> bugReportRouteYaml(std::string_view map, bool hasSave) {
  YamlNode root = YamlNode::mapping();
  root.set("name", YamlNode::scalar("report"));
  root.set("device", YamlNode::scalar("keyboard"));
  root.set("timeout", number(RouteTimeout));
  if (hasSave) {
    root.set("load_save", text(std::string{ReportSaveFile}));
  }
  YamlNode wait = YamlNode::mapping();
  wait.set("map", text(std::string{map}));
  wait.set("seconds", number(RouteSettleSeconds));
  wait.set("timeout", number(RouteMapTimeout));
  YamlNode waitStep = YamlNode::mapping();
  waitStep.set("wait", std::move(wait));
  YamlNode shotStep = YamlNode::mapping();
  shotStep.set("screenshot", YamlNode::scalar("report"));
  YamlNode steps = YamlNode::sequence();
  steps.append(std::move(waitStep));
  steps.append(std::move(shotStep));
  root.set("steps", std::move(steps));
  auto body = core::emitYaml(root);
  if (!body) {
    return body;
  }
  return std::format("# Plays the bug report's spot again: dwe-game --content <folder> --autotest "
                     "route.yaml\n# Add the steps that show the bug after the wait.\n{}",
                     *body);
}

} // namespace dwe::app
