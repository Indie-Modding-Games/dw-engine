#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <system_error>
#include <utility>

namespace dwe::assets {

namespace {

using core::YamlNode;

constexpr std::size_t MaxPackIdLength = 64;

constexpr std::array<std::pair<std::string_view, Layer>, 4> LayerNames = {{
    {"base", Layer::Base},
    {"remaster", Layer::Remaster},
    {"mods", Layer::Mods},
    {"user", Layer::User},
}};

std::unexpected<core::Error> failAt(const YamlNode& node, std::string_view problem) {
    return core::fail(std::format("line {}, column {}: {}", node.line(), node.column(), problem));
}

// Reads a whole decimal number; leading zeros are refused, as semver does.
template <typename T>
std::optional<T> parseNumber(std::string_view text) {
    if (text.size() > 1 && text.front() == '0') {
        return std::nullopt;
    }
    T value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

core::Result<std::string> readText(const YamlNode& node, std::string_view key) {
    if (!node.isScalar()) {
        return failAt(node, std::format("'{}' must be a single value", key));
    }
    return node.text();
}

core::Result<Version> readVersion(const YamlNode& node, std::string_view key) {
    auto text = readText(node, key);
    if (!text) {
        return std::unexpected{text.error()};
    }
    auto version = parseVersion(*text);
    if (!version) {
        return failAt(node, version.error().message);
    }
    return *version;
}

core::Result<std::string> readPackId(const YamlNode& node, std::string_view key) {
    auto id = readText(node, key);
    if (!id) {
        return id;
    }
    if (auto checked = checkPackId(*id); !checked) {
        return failAt(node, checked.error().message);
    }
    return id;
}

core::Result<PackDependency> readDependency(const YamlNode& node) {
    if (!node.isMapping()) {
        return failAt(node, "a dependency must be a mapping with 'id' and 'min_version'");
    }
    PackDependency dependency;
    bool hasId = false;
    bool hasMinVersion = false;
    for (const core::YamlEntry& entry : node.entries()) {
        if (entry.key == "id") {
            auto id = readPackId(entry.value, entry.key);
            if (!id) {
                return std::unexpected{id.error()};
            }
            dependency.id = std::move(*id);
            hasId = true;
        } else if (entry.key == "min_version") {
            auto version = readVersion(entry.value, entry.key);
            if (!version) {
                return std::unexpected{version.error()};
            }
            dependency.minVersion = *version;
            hasMinVersion = true;
        } else {
            return failAt(entry.value, std::format("unknown dependency key '{}'", entry.key));
        }
    }
    if (!hasId || !hasMinVersion) {
        return failAt(node, "a dependency needs 'id' and 'min_version'");
    }
    return dependency;
}

core::Result<std::vector<PackDependency>> readDependencies(const YamlNode& node, std::string_view packId) {
    if (!node.isSequence()) {
        return failAt(node, "'dependencies' must be a list");
    }
    std::vector<PackDependency> dependencies;
    for (const YamlNode& item : node.items()) {
        auto dependency = readDependency(item);
        if (!dependency) {
            return std::unexpected{dependency.error()};
        }
        if (dependency->id == packId) {
            return failAt(item, "a pack cannot depend on itself");
        }
        for (const PackDependency& other : dependencies) {
            if (other.id == dependency->id) {
                return failAt(item, std::format("pack '{}' is listed twice", other.id));
            }
        }
        dependencies.push_back(std::move(*dependency));
    }
    return dependencies;
}

// `schemas`: a mapping from schema names to lists of id patterns.
core::Result<std::vector<SchemaRule>> readSchemaRules(const YamlNode& node) {
    if (!node.isMapping()) {
        return failAt(node, "'schemas' must be a mapping from schema names to lists of id patterns");
    }
    std::vector<SchemaRule> rules;
    for (const core::YamlEntry& entry : node.entries()) {
        if (auto name = checkSchemaName(entry.key); !name) {
            return failAt(entry.value, name.error().message);
        }
        if (!entry.value.isSequence() || entry.value.items().empty()) {
            return failAt(entry.value, std::format("schema '{}' needs a list of id patterns", entry.key));
        }
        SchemaRule rule{.schema = entry.key, .ids = {}};
        for (const YamlNode& item : entry.value.items()) {
            if (!item.isScalar()) {
                return failAt(item, "an id pattern must be a single value");
            }
            if (auto pattern = checkIdPattern(item.text()); !pattern) {
                return failAt(item, pattern.error().message);
            }
            rule.ids.push_back(item.text());
        }
        rules.push_back(std::move(rule));
    }
    return rules;
}

} // namespace

core::Result<void> checkSchemaName(std::string_view name) {
    const auto isNameCharacter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; };
    if (name.empty() || name.size() > MaxPackIdLength || !std::ranges::all_of(name, isNameCharacter)) {
        return core::fail(
            std::format("schema name '{}' must have 1 to {} characters of a-z, 0-9 and '_'", name, MaxPackIdLength));
    }
    return {};
}

core::Result<Version> parseVersion(std::string_view text) {
    const std::size_t firstDot = text.find('.');
    const std::size_t secondDot = firstDot == std::string_view::npos ? firstDot : text.find('.', firstDot + 1);
    if (secondDot == std::string_view::npos) {
        return core::fail(std::format("version '{}' is not MAJOR.MINOR.PATCH", text));
    }
    const auto major = parseNumber<std::uint32_t>(text.substr(0, firstDot));
    const auto minor = parseNumber<std::uint32_t>(text.substr(firstDot + 1, secondDot - firstDot - 1));
    const auto patch = parseNumber<std::uint32_t>(text.substr(secondDot + 1));
    if (!major || !minor || !patch) {
        return core::fail(std::format("version '{}' is not MAJOR.MINOR.PATCH", text));
    }
    return Version{.major = *major, .minor = *minor, .patch = *patch};
}

std::string formatVersion(const Version& version) {
    return std::format("{}.{}.{}", version.major, version.minor, version.patch);
}

std::optional<Layer> parseLayer(std::string_view name) {
    for (const auto& [text, layer] : LayerNames) {
        if (text == name) {
            return layer;
        }
    }
    return std::nullopt;
}

std::string_view layerName(Layer layer) {
    for (const auto& [text, value] : LayerNames) {
        if (value == layer) {
            return text;
        }
    }
    return "unknown";
}

core::Result<void> checkPackId(std::string_view id) {
    if (id.empty() || id.size() > MaxPackIdLength) {
        return core::fail(std::format("pack id '{}' must have 1 to {} characters", id, MaxPackIdLength));
    }
    const auto isIdCharacter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; };
    if (!std::ranges::all_of(id, isIdCharacter)) {
        return core::fail(std::format("pack id '{}' may only use a-z, 0-9 and '_'", id));
    }
    return {};
}

core::Result<PackManifest> parsePackManifest(std::string_view text) {
    auto root = core::parseYaml(text);
    if (!root) {
        return std::unexpected{root.error()};
    }
    if (!root->isMapping()) {
        return failAt(*root, "a manifest must be a mapping");
    }

    PackManifest manifest;
    bool hasId = false;
    bool hasVersion = false;
    bool hasLayer = false;
    const YamlNode* dependencies = nullptr;
    for (const core::YamlEntry& entry : root->entries()) {
        const YamlNode& value = entry.value;
        if (entry.key == "id") {
            auto id = readPackId(value, entry.key);
            if (!id) {
                return std::unexpected{id.error()};
            }
            manifest.id = std::move(*id);
            hasId = true;
        } else if (entry.key == "version") {
            auto version = readVersion(value, entry.key);
            if (!version) {
                return std::unexpected{version.error()};
            }
            manifest.version = *version;
            hasVersion = true;
        } else if (entry.key == "layer") {
            auto name = readText(value, entry.key);
            if (!name) {
                return std::unexpected{name.error()};
            }
            const auto layer = parseLayer(*name);
            if (!layer) {
                return failAt(value, std::format("unknown layer '{}': use base, remaster, mods or user", *name));
            }
            manifest.layer = *layer;
            hasLayer = true;
        } else if (entry.key == "priority") {
            auto number = readText(value, entry.key);
            if (!number) {
                return std::unexpected{number.error()};
            }
            const auto priority = parseNumber<std::int32_t>(*number);
            if (!priority) {
                return failAt(value, std::format("priority '{}' is not a whole number", *number));
            }
            manifest.priority = *priority;
        } else if (entry.key == "name") {
            auto name = readText(value, entry.key);
            if (!name) {
                return std::unexpected{name.error()};
            }
            manifest.name = std::move(*name);
        } else if (entry.key == "description") {
            auto description = readText(value, entry.key);
            if (!description) {
                return std::unexpected{description.error()};
            }
            manifest.description = std::move(*description);
        } else if (entry.key == "dependencies") {
            dependencies = &value;
        } else if (entry.key == "schemas") {
            auto rules = readSchemaRules(value);
            if (!rules) {
                return std::unexpected{rules.error()};
            }
            manifest.schemas = std::move(*rules);
        } else {
            return failAt(value, std::format("unknown key '{}'", entry.key));
        }
    }
    if (!hasId || !hasVersion || !hasLayer) {
        return failAt(*root, "a manifest needs 'id', 'version' and 'layer'");
    }
    if (dependencies != nullptr) {
        auto list = readDependencies(*dependencies, manifest.id);
        if (!list) {
            return std::unexpected{list.error()};
        }
        manifest.dependencies = std::move(*list);
    }
    return manifest;
}

} // namespace dwe::assets
