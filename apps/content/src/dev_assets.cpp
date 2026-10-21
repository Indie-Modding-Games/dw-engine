#include <dwe/assets/file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/content/dev_assets.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace dwe::content {
namespace {

constexpr std::string_view DiscImageKey = "disc_image";
constexpr std::string_view ExtractedContentKey = "extracted_content";
constexpr std::string_view DecompRootKey = "decomp_root";

// A path written in the config, as UTF-8 (on Windows a char path would be the ANSI code page), relative to the
// config's folder unless it is absolute, without the . and .. that the logs would show.
std::filesystem::path configPath(std::string_view text, const std::filesystem::path& folder) {
    const std::u8string utf8(text.begin(), text.end());
    const std::filesystem::path path{utf8};
    return (path.is_absolute() ? path : folder / path).lexically_normal();
}

std::optional<std::filesystem::path> findConfig(std::span<const std::filesystem::path> folders) {
    for (const std::filesystem::path& folder : folders) {
        std::filesystem::path file = folder / DevAssetsConfigName;
        std::error_code error;
        if (std::filesystem::is_regular_file(file, error)) {
            return file;
        }
    }
    return std::nullopt;
}

std::string searchedFiles(std::span<const std::filesystem::path> folders) {
    std::string text;
    for (const std::filesystem::path& folder : folders) {
        if (!text.empty()) {
            text += messages::ListSeparator;
        }
        text += assets::pathText(folder / DevAssetsConfigName);
    }
    return text;
}

core::Result<std::string> readConfig(const std::filesystem::path& file) {
    const assets::DiskFileSystem disk;
    auto bytes = disk.readFile(file);
    if (!bytes) {
        return core::fail(
            core::formatMessage(messages::CannotReadDevAssetsConfig, assets::pathText(file), bytes.error().message));
    }
    return std::string{reinterpret_cast<const char*>(bytes->data()), bytes->size()};
}

void logSources(const std::filesystem::path& config, const assets::DevAssetSources& sources) {
    core::logWarning("{}", core::formatMessage(messages::DevAssetsOn, assets::pathText(config)));
    for (const assets::DevSource& source : sources.list()) {
        core::logWarning("{}", core::formatMessage(messages::DevAssetsSource, assets::devSourceKindName(source.kind),
                                                   assets::pathText(source.root)));
    }
}

} // namespace

std::vector<std::filesystem::path> devAssetsConfigFolders(const std::optional<std::filesystem::path>& userData) {
    std::vector<std::filesystem::path> folders;
    std::error_code error;
    std::filesystem::path current = std::filesystem::current_path(error);
    if (error) {
        core::logWarning("{}", core::formatMessage(messages::NoCurrentFolder, error.message()));
    } else {
        folders.push_back(std::move(current));
    }
    if (userData) {
        folders.push_back(*userData);
    }
    return folders;
}

core::Result<assets::DevAssetSources> parseDevAssetsConfig(std::string_view yaml, const std::filesystem::path& folder) {
    auto tree = core::parseYaml(yaml);
    if (!tree) {
        return std::unexpected{tree.error()};
    }
    assets::YamlReader reader;
    assets::DevAssetSources sources;
    if (reader.expectKeys(*tree, messages::DevAssetsConfigWhat, {},
                          {DiscImageKey, ExtractedContentKey, DecompRootKey})) {
        if (const core::YamlNode* disc = tree->find(DiscImageKey)) {
            sources.discImage = configPath(reader.text(*disc), folder);
        }
        if (const core::YamlNode* extracted = tree->find(ExtractedContentKey)) {
            for (const core::YamlNode& item : reader.sequence(*extracted, ExtractedContentKey)) {
                sources.extractedContent.push_back(configPath(reader.text(item), folder));
            }
        }
        if (const core::YamlNode* decomp = tree->find(DecompRootKey)) {
            sources.decompRoot = configPath(reader.text(*decomp), folder);
        }
        if (sources.empty()) {
            reader.fail(*tree, messages::DevAssetsConfigEmpty);
        }
    }
    return reader.finish(std::move(sources), messages::DevAssetsConfigWhat);
}

core::Result<assets::DevAssetSources> loadDevAssets(std::span<const std::filesystem::path> configFolders) {
    const std::optional<std::filesystem::path> config = findConfig(configFolders);
    if (!config) {
        return core::fail(
            core::formatMessage(messages::NoDevAssetsConfig, DevAssetsOption, searchedFiles(configFolders)));
    }
    auto text = readConfig(*config);
    if (!text) {
        return std::unexpected{text.error()};
    }
    auto sources = parseDevAssetsConfig(*text, config->parent_path());
    if (!sources) {
        return core::fail(core::formatMessage(messages::InFile, assets::pathText(*config), sources.error().message));
    }
    if (auto valid = assets::validateDevAssetSources(*sources); !valid) {
        return std::unexpected{valid.error()};
    }
    logSources(*config, *sources);
    return sources;
}

} // namespace dwe::content
