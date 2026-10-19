#include "disc_description.hpp"

#include <dwe/core/sha1.hpp>
#include <dwe/formats/disc/boot.hpp>
#include <dwe/formats/disc/known_releases.hpp>
#include <dwe/formats/exe/executable_layout.hpp>

#include <format>
#include <utility>

namespace dwe::dwx {

DiscDescription describeDisc(formats::disc::SectorSource& source, const formats::disc::IsoVolume& volume) {
    DiscDescription description{
        .info = {.volumeId = volume.volumeId, .executablePath = {}, .executableSha1 = {}, .version = {}},
        .tables = std::nullopt,
        .warnings = {}};
    DiscInfo& info = description.info;
    const auto executable = formats::disc::findBootExecutable(source, volume);
    if (!executable) {
        description.warnings.push_back(executable.error().message);
        return description;
    }
    info.executablePath = executable->path;
    const auto bytes = formats::disc::readIsoFile(source, *executable);
    if (!bytes) {
        description.warnings.push_back(bytes.error().message);
        return description;
    }
    const auto digest = core::sha1(*bytes);
    info.executableSha1 = core::toHex(digest);
    if (const auto release = formats::disc::findKnownRelease(digest)) {
        info.version = release->version;
    }
    const auto layout = formats::exe::findExecutableLayout(info.version);
    if (!layout) {
        description.warnings.emplace_back("the tables of this executable are not known; assets get numbers");
        return description;
    }
    auto tables = readGameTables(*bytes, *layout);
    if (!tables) {
        description.warnings.push_back(
            std::format("the tables of version {} cannot be read: {}", info.version, tables.error().message));
        return description;
    }
    description.tables = std::move(*tables);
    return description;
}

} // namespace dwe::dwx
