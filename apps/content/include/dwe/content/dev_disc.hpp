#pragma once

#include <dwe/assets/dev_asset_sources.hpp>
#include <dwe/core/error.hpp>
#include <dwe/platform/process.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The disc image of the dev assets, read through dwx: only dwx reads disc images (dependency rule 2), so the apps ask
// it for the disc's extracted content in a dev cache of the user's data folder and mount that like any extracted
// content folder (docs/architecture/assets-de-desarrollo.md).
namespace dwe::content {

// The folder of the user's data that holds the dev cache of extracted discs.
inline constexpr std::string_view DevCacheFolderName = "dev-cache";
// The tool that extracts discs.
inline constexpr std::string_view DwxProgram = "dwx";

// Runs a program and waits for it: platform::runProcess, or a stand-in in tests.
using ProcessRunner = std::function<core::Result<platform::ProcessResult>(std::span<const std::string> arguments)>;

// What reading the disc image takes: the dwx that extracts it, the cache it extracts into and how to run dwx.
struct DiscExtraction {
    std::filesystem::path dwx;
    std::filesystem::path cache;
    ProcessRunner run;
};

// Where dwx is looked for, in order: next to the running executable, then in the build tree, where each program has
// its own folder under apps/ (apps/game/dwe-game, apps/dwx/dwx).
[[nodiscard]] std::vector<std::filesystem::path> dwxCandidates(const std::filesystem::path& executableFolder);

// The first of the candidates that is a file. Fails naming them all when none is.
core::Result<std::filesystem::path> findDwx(std::span<const std::filesystem::path> candidates);

// How the running app reads a disc image: dwx found from its executable's folder, the cache in the user's data
// folder, run with platform::runProcess. Fails when either folder or dwx cannot be found.
core::Result<DiscExtraction> discExtraction(const std::optional<std::filesystem::path>& userData);

// The content folder that dwx extract --cache gives for the disc, extracting it first when its entry is not there.
core::Result<std::filesystem::path> extractedDiscContent(const std::filesystem::path& disc,
                                                         const DiscExtraction& extraction);

// The dev assets with the disc's extracted content after the configured extracted content folders, which win over it
// by pack id; all of them stay under every normal pack. Without a disc image they are returned as they are.
core::Result<assets::DevAssetSources> withDiscContent(assets::DevAssetSources sources,
                                                      const DiscExtraction& extraction);

} // namespace dwe::content
