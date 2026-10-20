#pragma once

#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/error.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/ruleset/ruleset.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace dwe::ruleset {

// Writes the values of `values` into the text of a section's file, each in place: the comments (the decomp
// citations), the order of the keys and the layout stay as they are. emitYaml cannot do that, since a parsed document
// keeps no comments, so only the scalars that changed are rewritten, each on its own line. `values` must have the
// shape of the file's document (the same keys and items): a change of shape is an error.
core::Result<std::string> patchSectionText(std::string_view fileText, const core::YamlNode& values);

// Writes a section of a ruleset back to the file of its pack, with patchSectionText. Returns the file written. The pack
// must be a folder on disk: a .pak archive is an error.
core::Result<std::filesystem::path> saveSection(const assets::VirtualFileSystem& vfs, const Section& section);

} // namespace dwe::ruleset
