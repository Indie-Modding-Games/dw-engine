#include <dwe/core/sha1.hpp>
#include <dwe/formats/disc/boot.hpp>
#include <dwe/formats/disc/disc_image.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/disc/known_releases.hpp>
#include <dwe/formats/exe/executable_layout.hpp>

#include <cstdio>
#include <optional>
#include <print>
#include <utility>

#include "base_pack.hpp"
#include "dwx.hpp"
#include "game_tables.hpp"

namespace dwe::dwx {

namespace {

// What the extraction knows about the disc before it looks at its files.
struct DiscDescription {
    DiscInfo info;
    std::optional<GameTables> tables;
};

// Identifies the version by the executable and reads its tables. An unknown executable leaves the version empty and
// the tables unread, so assets are named by number.
DiscDescription describeDisc(formats::disc::DiscImage& image, const formats::disc::IsoVolume& volume) {
    DiscDescription description{
        .info = {.volumeId = volume.volumeId, .executablePath = {}, .executableSha1 = {}, .version = {}},
        .tables = std::nullopt};
    DiscInfo& info = description.info;
    const auto executable = formats::disc::findBootExecutable(image, volume);
    if (!executable) {
        std::println(stderr, "dwx: warning: {}", executable.error().message);
        return description;
    }
    info.executablePath = executable->path;
    const auto bytes = formats::disc::readIsoFile(image, *executable);
    if (!bytes) {
        std::println(stderr, "dwx: warning: {}", bytes.error().message);
        return description;
    }
    const auto digest = core::sha1(*bytes);
    info.executableSha1 = core::toHex(digest);
    if (const auto release = formats::disc::findKnownRelease(digest)) {
        info.version = release->version;
    }
    const auto layout = formats::exe::findExecutableLayout(info.version);
    if (!layout) {
        std::println(stderr, "dwx: warning: the tables of this executable are not known; assets get numbers");
        return description;
    }
    auto tables = readGameTables(*bytes, *layout);
    if (!tables) {
        std::println(stderr, "dwx: warning: the tables of version {} cannot be read: {}", info.version,
                     tables.error().message);
        return description;
    }
    description.tables = std::move(*tables);
    return description;
}

double percent(std::uint64_t part, std::uint64_t whole) {
    return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
}

} // namespace

int extractDisc(const std::filesystem::path& path, const std::filesystem::path& out, bool force) {
    auto image = formats::disc::DiscImage::open(path);
    if (!image) {
        std::println(stderr, "dwx: {}", image.error().message);
        return ExitFailed;
    }
    const auto volume = formats::disc::readIsoVolume(*image);
    if (!volume) {
        std::println(stderr, "dwx: {}", volume.error().message);
        return ExitFailed;
    }
    const DiscDescription description = describeDisc(*image, *volume);
    const DiscInfo& info = description.info;
    std::println("Disc: {}, {} files, executable {} ({})", info.volumeId, volume->files.size(), info.executablePath,
                 info.version.empty() ? "unknown version" : info.version);

    if (description.tables) {
        std::println("Tables: {} Digimon, {} maps; assets take their names", description.tables->ids.digimon.size(),
                     description.tables->ids.maps.size());
    }

    const Disc disc{*image, *volume, info, description.tables ? &*description.tables : nullptr};
    const auto run = runExtractors(disc, extractors());
    if (!run) {
        std::println(stderr, "dwx: {}", run.error().message);
        return ExitFailed;
    }
    if (auto prepared = prepareOutputFolder(out, force); !prepared) {
        std::println(stderr, "dwx: {}", prepared.error().message);
        return ExitFailed;
    }
    if (auto written = writeBasePack(out, *run, info); !written) {
        std::println(stderr, "dwx: {}", written.error().message);
        return ExitFailed;
    }

    const CoverageSummary summary = summarize(*run);
    std::println("Base pack: {}", out.string());
    std::println("Files: {} total, {} extracted, {} partial, {} pending", summary.files, summary.extracted,
                 summary.partial, summary.pending);
    std::println("Bytes covered: {} of {} ({:.1f} %)", summary.bytesCovered, summary.bytes,
                 percent(summary.bytesCovered, summary.bytes));
    std::println("Outputs: {} ({} repeat another output byte for byte)", summary.outputs, summary.duplicates);

    const auto assets = checkBasePack(out, *run);
    if (!assets) {
        std::println(stderr, "dwx: engine check failed: {}", assets.error().message);
        return ExitFailed;
    }
    std::println("Engine check: the pack mounts with {} assets and every output resolves", *assets);
    return ExitOk;
}

} // namespace dwe::dwx
