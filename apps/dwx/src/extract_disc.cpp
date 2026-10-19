#include <dwe/core/sha1.hpp>
#include <dwe/formats/disc/boot.hpp>
#include <dwe/formats/disc/disc_image.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/disc/known_releases.hpp>

#include <cstdio>
#include <print>

#include "base_pack.hpp"
#include "dwx.hpp"

namespace dwe::dwx {

namespace {

// Identifies the version by the executable; an unknown executable only leaves the version empty.
DiscInfo describeDisc(formats::disc::DiscImage& image, const formats::disc::IsoVolume& volume) {
    DiscInfo info{.volumeId = volume.volumeId, .executablePath = {}, .executableSha1 = {}, .version = {}};
    const auto executable = formats::disc::findBootExecutable(image, volume);
    if (!executable) {
        std::println(stderr, "dwx: warning: {}", executable.error().message);
        return info;
    }
    info.executablePath = executable->path;
    const auto bytes = formats::disc::readIsoFile(image, *executable);
    if (!bytes) {
        std::println(stderr, "dwx: warning: {}", bytes.error().message);
        return info;
    }
    const auto digest = core::sha1(*bytes);
    info.executableSha1 = core::toHex(digest);
    if (const auto release = formats::disc::findKnownRelease(digest)) {
        info.version = release->version;
    }
    return info;
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
    const DiscInfo info = describeDisc(*image, *volume);
    std::println("Disc: {}, {} files, executable {} ({})", info.volumeId, volume->files.size(), info.executablePath,
                 info.version.empty() ? "unknown version" : info.version);

    const Disc disc{*image, *volume, info};
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
    std::println("Outputs: {}", summary.outputs);

    const auto assets = checkBasePack(out, *run);
    if (!assets) {
        std::println(stderr, "dwx: engine check failed: {}", assets.error().message);
        return ExitFailed;
    }
    std::println("Engine check: the pack mounts with {} assets and every output resolves", *assets);
    return ExitOk;
}

} // namespace dwe::dwx
