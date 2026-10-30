#include "state_trace.hpp"

#include <dwe/assets/map_document.hpp>

#include <cmath>
#include <format>
#include <iterator>
#include <string_view>
#include <utility>

namespace dwe::app {

namespace {

// The original's axes: X as is, Y and Z negated (core::fromOriginalAxes the other way).
std::int32_t originalUnits(float world) {
  return static_cast<std::int32_t>(std::lround(world));
}

void appendText(std::string& out, std::string_view text) {
  out += '"';
  for (const char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    out += c;
  }
  out += '"';
}

void appendCharacter(std::string& out, const TraceCharacter& character) {
  std::format_to(std::back_inserter(out), R"({{"x":{},"y":{},"z":{},"yaw":{})",
                 originalUnits(character.position.x), originalUnits(-character.position.y),
                 originalUnits(-character.position.z), assets::yawToOriginal(character.yaw));
  if (character.id) {
    std::format_to(std::back_inserter(out), R"(,"id":{})", *character.id);
  }
  out += '}';
}

void appendFighter(std::string& out, const TraceFighter& fighter) {
  std::format_to(std::back_inserter(out),
                 R"({{"x":{},"z":{},"yaw":{},"hp":{},"mp":{},"max_hp":{},"max_mp":{},)"
                 R"("off":{},"def":{},"spd":{},"brain":{},"pose":)",
                 fighter.x, -fighter.z, assets::yawToOriginal(fighter.yaw), fighter.hp, fighter.mp,
                 fighter.maxHp, fighter.maxMp, fighter.offense, fighter.defense, fighter.speed,
                 fighter.brain);
  appendText(out, fighter.pose);
  out += '}';
}

} // namespace

std::string traceLine(const TraceFrame& frame) {
  std::string out;
  std::format_to(std::back_inserter(out), R"({{"src":"dwe","f":{},"time":{:.4f},"in":{},"map":)",
                 frame.tick, frame.seconds, frame.buttons);
  appendText(out, frame.map);
  std::format_to(std::back_inserter(out), R"(,"mode":"{}")", frame.battle ? "battle" : "field");
  if (frame.player) {
    out += R"(,"player":)";
    appendCharacter(out, *frame.player);
  }
  if (frame.partner) {
    out += R"(,"partner":)";
    appendCharacter(out, *frame.partner);
  }
  out += R"(,"npcs":[)";
  for (std::size_t index = 0; index < frame.npcs.size(); ++index) {
    if (index > 0) {
      out += ',';
    }
    appendCharacter(out, frame.npcs[index]);
  }
  out += ']';
  if (frame.battle) {
    out += R"(,"battle":{"phase":)";
    appendText(out, frame.battle->phase);
    std::format_to(std::back_inserter(out), R"(,"frame":{},"fighters":[)", frame.battle->frame);
    for (std::size_t index = 0; index < frame.battle->fighters.size(); ++index) {
      if (index > 0) {
        out += ',';
      }
      appendFighter(out, frame.battle->fighters[index]);
    }
    out += "]}";
  }
  out += '}';
  return out;
}

core::Result<StateTrace> StateTrace::open(const std::filesystem::path& path) {
  std::ofstream file{path, std::ios::trunc};
  if (!file) {
    return core::fail(std::format("cannot write the trace {}", path.string()));
  }
  file << R"({"src":"dwe","meta":true,"hz":60})" << '\n';
  return StateTrace{std::move(file)};
}

void StateTrace::write(const TraceFrame& frame) {
  file_ << traceLine(frame) << '\n';
}

} // namespace dwe::app
