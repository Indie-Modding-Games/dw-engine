#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/save/sections.hpp>

#include <string_view>

#include "session_state.hpp"

// The session's save sections: each wraps one part of the playthrough in a packages/game/save
// SectionSerializer.
namespace dwe::session::sections {

inline constexpr std::string_view ClockName = "clock";
inline constexpr std::string_view PartnerName = "partner";
inline constexpr std::string_view TamerName = "tamer";
inline constexpr std::string_view FieldName = "field";
inline constexpr std::string_view RandomName = "random";

// The game clock: its frames since year 0 and the fraction of a frame real time has run past them.
class ClockSave final : public game::save::SectionSerializer {
public:
  explicit ClockSave(SessionState& state) : state_(&state) {}
  [[nodiscard]] core::Result<core::YamlNode> write() const override;
  core::Result<void> read(const core::YamlNode& state) override;

private:
  SessionState* state_;
};

// The partner's state, written by packages/game/partner (toYaml, fromYaml), which versions it
// itself.
class PartnerSave final : public game::save::SectionSerializer {
public:
  explicit PartnerSave(SessionState& state) : state_(&state) {}
  [[nodiscard]] core::Result<core::YamlNode> write() const override;
  core::Result<void> read(const core::YamlNode& state) override;

private:
  SessionState* state_;
};

// The tamer's level.
class TamerSave final : public game::save::SectionSerializer {
public:
  explicit TamerSave(SessionState& state) : state_(&state) {}
  [[nodiscard]] core::Result<core::YamlNode> write() const override;
  core::Result<void> read(const core::YamlNode& state) override;

private:
  SessionState* state_;
};

// The map and the player's place on it; empty before the player entered any map.
class FieldSave final : public game::save::SectionSerializer {
public:
  explicit FieldSave(SessionState& state) : state_(&state) {}
  [[nodiscard]] core::Result<core::YamlNode> write() const override;
  core::Result<void> read(const core::YamlNode& state) override;

private:
  SessionState* state_;
};

// Where each system's random numbers are, so a loaded game rolls what the saved one would have.
class RandomSave final : public game::save::SectionSerializer {
public:
  explicit RandomSave(SessionState& state) : state_(&state) {}
  [[nodiscard]] core::Result<core::YamlNode> write() const override;
  core::Result<void> read(const core::YamlNode& state) override;

private:
  SessionState* state_;
};

// A section to register: its name, serializer and versions.
struct SectionEntry {
  std::string_view name;
  game::save::SectionSerializer* serializer = nullptr;
  game::save::MigrationChain migrations;
};

// Every section of the session, registered in this order: the partner is read before its random
// numbers.
struct SessionSerializers {
  explicit SessionSerializers(SessionState& state)
      : clock(state), partner(state), tamer(state), field(state), random(state) {}

  // Registers them all.
  core::Result<void> addTo(game::save::SaveSections& sections);

  ClockSave clock;
  PartnerSave partner;
  TamerSave tamer;
  FieldSave field;
  RandomSave random;
};

} // namespace dwe::session::sections
