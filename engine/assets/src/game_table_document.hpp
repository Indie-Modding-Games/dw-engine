#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>

#include <string_view>

namespace dwe::assets {

class VirtualFileSystem;

// Parses the YAML text of a game table extracted by dwx; a syntax error names the table.
core::Result<core::YamlNode> parseTableDocument(std::string_view yaml, std::string_view what);

// Reads and parses a game table of the packs by its asset id.
core::Result<core::YamlNode> readTableDocument(const VirtualFileSystem& vfs, const AssetId& id, std::string_view what);

} // namespace dwe::assets
