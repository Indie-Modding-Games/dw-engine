#pragma once

#include <dwe/core/error.hpp>

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::assets {

// A semantic version, MAJOR.MINOR.PATCH. Pre-release and build suffixes are not accepted.
struct Version {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;

    friend auto operator<=>(const Version&, const Version&) = default;
};

// Reads a version such as `1.4.0`.
core::Result<Version> parseVersion(std::string_view text);

// Writes a version as MAJOR.MINOR.PATCH.
std::string formatVersion(const Version& version);

// The content layers, from the bottom up: a higher layer overrides the assets of the lower ones.
enum class Layer : std::uint8_t { Base = 1, Remaster = 2, Mods = 3, User = 4 };

// Reads a layer by its name in a manifest: base, remaster, mods or user.
std::optional<Layer> parseLayer(std::string_view name);

// The name of a layer in a manifest.
std::string_view layerName(Layer layer);

// Checks a pack id: 1 to 64 lowercase letters, digits and underscores.
core::Result<void> checkPackId(std::string_view id);

// A pack this pack needs, at least at the given version.
struct PackDependency {
    std::string id;
    Version minVersion;
};

// The manifest of a pack, read from `pack.yaml` at its root.
struct PackManifest {
    std::string id;
    Version version;
    Layer layer = Layer::Mods;
    // Orders the packs of one layer: the higher one wins.
    std::int32_t priority = 0;
    std::string name;
    std::string description;
    std::vector<PackDependency> dependencies;
};

// Parses and checks the text of a manifest. Unknown keys are errors, so a misspelt key never goes unnoticed.
core::Result<PackManifest> parsePackManifest(std::string_view text);

} // namespace dwe::assets
