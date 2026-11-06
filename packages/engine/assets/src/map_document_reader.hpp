#pragma once

#include <dwe/assets/map_document.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>

namespace dwe::assets {

class YamlReader;

// Reads a map's data from its parsed tree: parseMapDocument without the parsing, so a patch can
// change the tree first (map_patch.hpp).
core::Result<MapDocument> readMapDocument(const core::YamlNode& root);

// Reads a collision grid in the form of the data's `collision`.
MapCollision readMapCollision(YamlReader& reader, const core::YamlNode& node);

} // namespace dwe::assets
