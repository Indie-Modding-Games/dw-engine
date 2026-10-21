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

// The Lua API version a pack's scripts are written for: `api: "1.x"` (any 1.y) or `api: "1.3"` (1.3 or a later 1.y) in
// a manifest. The engine runs them when its API has the same major version and at least that minor one
// (checkApiCompatibility in lua_api.hpp).
struct ApiRequirement {
    std::uint32_t major = 0;
    // The oldest minor version with what the scripts use; none for any.
    std::optional<std::uint32_t> minimumMinor;

    friend bool operator==(const ApiRequirement&, const ApiRequirement&) = default;
};

// Reads an API requirement such as `1.x` or `1.3`.
core::Result<ApiRequirement> parseApiRequirement(std::string_view text);

// Writes an API requirement as MAJOR.x or MAJOR.MINOR.
std::string formatApiRequirement(const ApiRequirement& requirement);

// The content layers, from the bottom up: a higher layer overrides the assets of the lower ones.
enum class Layer : std::uint8_t { Base = 1, Remaster = 2, Mods = 3, User = 4 };

// Reads a layer by its name in a manifest: base, remaster, mods or user.
std::optional<Layer> parseLayer(std::string_view name);

// The name of a layer in a manifest.
std::string_view layerName(Layer layer);

// Where a pack was found, which decides whether its code may run as the engine: the engine's own folders (the content
// folder, the team's packs, the dev assets) or anywhere else, such as a mods folder. A pack's manifest never decides
// it, since a pack writes its own manifest. Unknown means Mod: deny by default. What each privilege needs, the
// origin and a layer, is decided where the privilege is used.
enum class PackOrigin : std::uint8_t { Mod, Engine };

// An origin in a message: "a mod" or "an engine pack".
std::string_view originName(PackOrigin origin);

// Whether a layer is one of the engine's own, base or remaster. Only a pack of engine origin (PackOrigin::Engine) may
// declare one.
[[nodiscard]] bool isEngineLayer(Layer layer);

// Whether a pack has the base's rights: adding ids without a namespace, giving indices and writing the original's raw
// warp targets. Only the engine's own base packs have them: where a pack was found decides, not the layer its manifest
// declares.
[[nodiscard]] bool hasBaseRights(PackOrigin origin, Layer layer);

// Checks a pack id: 1 to 64 lowercase letters, digits and underscores.
core::Result<void> checkPackId(std::string_view id);

// Checks the name of a schema: 1 to 64 lowercase letters, digits and underscores.
core::Result<void> checkSchemaName(std::string_view name);

// A pack this pack needs, at least at the given version.
struct PackDependency {
    std::string id;
    Version minVersion;
};

// The YAML documents of a pack whose ids match one of the patterns (see matchesIdPattern) follow the JSON Schema
// content/schemas/<schema>.schema.json. The rules of every mounted pack apply to every pack, so an asset that
// overrides another one still follows its schema.
struct SchemaRule {
    std::string schema;
    std::vector<std::string> ids;
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
    std::vector<SchemaRule> schemas;
    // The Lua API the pack's scripts need, when it declares one.
    std::optional<ApiRequirement> api;
};

// Checks that a mod declares a mod layer, mods or user: the engine's layers belong to the engine's own packs.
core::Result<void> checkModLayer(const PackManifest& manifest);

// Parses and checks the text of a manifest. Unknown keys are errors, so a misspelt key never goes unnoticed.
core::Result<PackManifest> parsePackManifest(std::string_view text);

} // namespace dwe::assets
