#include <dwe/core/message.hpp>
#include <dwe/formats/disc/disc_image.hpp>
#include <dwe/formats/disc/iso9660.hpp>

#include <cstdio>
#include <print>

#include "base_pack.hpp"
#include "disc_description.hpp"
#include "dwx.hpp"
#include "messages.hpp"
#include "upscale.hpp"

namespace dwe::dwx {

namespace {

double percent(std::uint64_t part, std::uint64_t whole) {
    return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
}

} // namespace

core::Result<void> extractBasePack(const std::filesystem::path& path, const std::filesystem::path& out, bool force,
                                   std::FILE* report) {
    auto image = formats::disc::DiscImage::open(path);
    if (!image) {
        return std::unexpected{image.error()};
    }
    const auto volume = formats::disc::readIsoVolume(*image);
    if (!volume) {
        return std::unexpected{volume.error()};
    }
    const DiscDescription description = describeDisc(*image, *volume);
    for (const auto& warning : description.warnings) {
        std::println(stderr, "dwx: warning: {}", warning);
    }
    const DiscInfo& info = description.info;
    std::println(report, "Disc: {}, {} files, executable {} ({})", info.volumeId, volume->files.size(),
                 info.executablePath, info.version.empty() ? "unknown version" : info.version);

    if (description.tables) {
        std::println(report, "Tables: {} Digimon, {} maps; assets take their names",
                     description.tables->ids.digimon.size(), description.tables->ids.maps.size());
    }

    const Disc disc{*image, *volume, info, description.tables ? &*description.tables : nullptr};
    const auto run = runExtractors(disc, extractors());
    if (!run) {
        return std::unexpected{run.error()};
    }
    if (auto prepared = prepareOutputFolder(out, force); !prepared) {
        return prepared;
    }
    if (auto written = writeBasePack(out, *run, info); !written) {
        return written;
    }

    const CoverageSummary summary = summarize(*run);
    std::println(report, "Base pack: {}", out.string());
    std::println(report, "Files: {} total, {} extracted, {} partial, {} pending", summary.files, summary.extracted,
                 summary.partial, summary.pending);
    std::println(report, "Bytes covered: {} of {} ({:.1f} %)", summary.bytesCovered, summary.bytes,
                 percent(summary.bytesCovered, summary.bytes));
    std::println(report, "Outputs: {} ({} repeat another output byte for byte)", summary.outputs, summary.duplicates);

    const auto assets = checkBasePack(out, *run);
    if (!assets) {
        return core::fail(core::formatMessage(messages::EngineCheckFailed, assets.error().message));
    }
    std::println(report, "Engine check: the pack mounts with {} assets, every output resolves and every asset opens",
                 *assets);
    return runBundledRecipes(out, report);
}

int extractDisc(const std::filesystem::path& path, const std::filesystem::path& out, bool force) {
    if (auto extracted = extractBasePack(path, out, force, stdout); !extracted) {
        std::println(stderr, "dwx: {}", extracted.error().message);
        return ExitFailed;
    }
    return ExitOk;
}

} // namespace dwe::dwx
