#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/partner/state.hpp>

#include <cstdint>

namespace dwe::game::partner {

// The version of the saved partner state; a later version adds fields and reads the older ones.
inline constexpr std::int32_t PartnerStateVersion = 1;

// The partner state as a YAML mapping, for the session's save.
core::YamlNode toYaml(const PartnerState& state);

// Reads a partner state toYaml wrote; errors name the line and column.
core::Result<PartnerState> fromYaml(const core::YamlNode& node);

} // namespace dwe::game::partner
