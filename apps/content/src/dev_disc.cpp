#include <dwe/assets/file_system.hpp>
#include <dwe/content/dev_disc.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "messages.hpp"

namespace dwe::content {
namespace {

// In the build tree, the folder of every program is apps/<program>.
constexpr std::string_view BuildTreeDwxFolder = "dwx";

constexpr std::string_view ExtractCommand = "extract";
constexpr std::string_view CacheOption = "--cache";

// How much of an unexpected output of dwx an error quotes, in bytes.
constexpr std::size_t MaxQuotedOutput = 200;
constexpr std::string_view CutMark = "...";

// The start of output for an error, cut at a whole UTF-8 character.
std::string quotedOutput(std::string_view output) {
    if (output.size() <= MaxQuotedOutput) {
        return std::string{output};
    }
    std::size_t end = MaxQuotedOutput;
    // A byte 10xxxxxx continues a character: the cut goes before the character it belongs to.
    while (end > 0 && (static_cast<unsigned char>(output[end]) & 0xC0U) == 0x80U) {
        --end;
    }
    return std::string{output.substr(0, end)} + std::string{CutMark};
}

std::string candidateList(std::span<const std::filesystem::path> candidates) {
    std::string text;
    for (const std::filesystem::path& candidate : candidates) {
        if (!text.empty()) {
            text += messages::ListSeparator;
        }
        text += assets::pathText(candidate);
    }
    return text;
}

// The one line dwx extract --cache writes, without its line end, as a path read from UTF-8.
std::optional<std::filesystem::path> printedFolder(std::string_view output) {
    while (output.ends_with('\n') || output.ends_with('\r')) {
        output.remove_suffix(1);
    }
    if (output.empty() || output.contains('\n')) {
        return std::nullopt;
    }
    return std::filesystem::path{std::u8string{output.begin(), output.end()}};
}

} // namespace

std::vector<std::filesystem::path> dwxCandidates(const std::filesystem::path& executableFolder) {
    const std::string program = platform::executableFileName(DwxProgram);
    return {executableFolder / program, executableFolder.parent_path() / BuildTreeDwxFolder / program};
}

core::Result<std::filesystem::path> findDwx(std::span<const std::filesystem::path> candidates) {
    for (const std::filesystem::path& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
    }
    return core::fail(core::formatMessage(messages::NoDwx, candidateList(candidates)));
}

core::Result<DiscExtraction> discExtraction(const std::optional<std::filesystem::path>& userData) {
    if (!userData) {
        return core::fail(std::string{messages::NoDevCacheFolder});
    }
    const auto folder = platform::executableFolder();
    if (!folder) {
        return std::unexpected{folder.error()};
    }
    auto dwx = findDwx(dwxCandidates(*folder));
    if (!dwx) {
        return std::unexpected{dwx.error()};
    }
    return DiscExtraction{.dwx = std::move(*dwx), .cache = *userData / DevCacheFolderName, .run = platform::runProcess};
}

core::Result<std::filesystem::path> extractedDiscContent(const std::filesystem::path& disc,
                                                         const DiscExtraction& extraction) {
    core::logWarning("{}", core::formatMessage(messages::ReadingDevDisc, assets::pathText(disc),
                                               assets::pathText(extraction.dwx), assets::pathText(extraction.cache)));
    const std::array<std::string, 5> command{assets::pathText(extraction.dwx), std::string{ExtractCommand},
                                             std::string{CacheOption}, assets::pathText(extraction.cache),
                                             assets::pathText(disc)};
    const auto result = extraction.run(command);
    if (!result) {
        return std::unexpected{result.error()};
    }
    if (result->exitCode != 0) {
        return core::fail(core::formatMessage(messages::DwxFailed, assets::pathText(disc), result->exitCode));
    }
    std::optional<std::filesystem::path> folder = printedFolder(result->output);
    if (!folder) {
        return core::fail(core::formatMessage(messages::DwxGaveNoFolder, quotedOutput(result->output)));
    }
    return std::move(*folder);
}

core::Result<assets::DevAssetSources> withDiscContent(assets::DevAssetSources sources,
                                                      const DiscExtraction& extraction) {
    if (!sources.discImage) {
        return sources;
    }
    auto folder = extractedDiscContent(*sources.discImage, extraction);
    if (!folder) {
        return std::unexpected{folder.error()};
    }
    core::logWarning("{}", core::formatMessage(messages::DevDiscContent, assets::pathText(*sources.discImage),
                                               assets::pathText(*folder)));
    sources.extractedContent.push_back(std::move(*folder));
    return sources;
}

} // namespace dwe::content
