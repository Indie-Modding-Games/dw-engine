#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/partner/state.hpp>

#include <cstdint>

namespace dwe::game::partner {

// The version of the saved partner state. fromYaml reads only this one; the session's save brings
// older ones up to it with the steps below (game/save MigrationChain).
inline constexpr std::int32_t PartnerStateVersion = 2;
// Version 2: the virus backup keeps the species too.
inline constexpr std::int32_t VirusBackupSpeciesVersion = 2;

// The partner state as a YAML mapping, for the session's save.
core::YamlNode toYaml(const PartnerState& state);

// Reads a partner state toYaml wrote; errors name the line and column.
core::Result<PartnerState> fromYaml(const core::YamlNode& node);

// The step from version 1 to VirusBackupSpeciesVersion: the virus backup's stats move under
// `stats`, next to an empty species, which saves of version 1 did not keep.
core::Result<core::YamlNode> addVirusBackupSpecies(core::YamlNode state);

} // namespace dwe::game::partner
