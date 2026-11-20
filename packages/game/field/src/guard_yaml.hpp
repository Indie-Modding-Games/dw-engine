#pragma once

#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/field/script_warps.hpp>

namespace dwe::game::field {

// A guard as field/script_warps and field/npc_spawns write it (`terms`, `unstructured`); an empty
// guard for none. A test the field does not read keeps only its name.
WarpGuard readGuard(assets::YamlReader& reader, const core::YamlNode* node);

} // namespace dwe::game::field
