#pragma once

#include <dwe/assets/asset_id.hpp>
#include <dwe/core/error.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/disc/sector.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "game_tables.hpp"

namespace dwe::dwx {

// Takes the parts of a streamed output, in order.
using ByteSink = std::function<core::Result<void>(std::span<const std::byte>)>;

// Gives a streamed output's bytes part by part to the sink and stops at the sink's first error.
using OutputStream = std::function<core::Result<void>(const ByteSink& sink)>;

// A file an extractor gives to the base pack. Its path is built from the asset id and the extension only, so no
// name read from the disc can take it out of the pack's folder.
struct OutputFile {
    assets::AssetId id;
    std::string extension; // lowercase letters and digits: "png", "yaml"
    std::vector<std::byte> bytes;
    // For an output too large to hold in memory (decoded video): when set, the pack writes what it gives instead of
    // bytes, so the file never sits whole in memory. It owns what it needs (never the disc), gives the same bytes
    // every time and may run on any thread. Streamed outputs are left out of the search for duplicates.
    OutputStream stream = nullptr;
};

// Returns the path of an output inside the pack, with '/' between folders: "card/000.png".
std::string outputPath(const OutputFile& output);

// What an extractor made of one disc file.
struct Extraction {
    std::vector<OutputFile> outputs;
    // What it did not understand or had to skip: byte ranges, fields, entries that did not decode, warnings of the
    // readers. A file with unknowns is reported as partly extracted.
    std::vector<std::string> unknowns;
    // What a reader of the report should know that is not missing: what the file holds that is not an asset, where
    // the data refers to something absent.
    std::vector<std::string> notes;
};

// What the tools know about the disc as a whole.
struct DiscInfo {
    std::string volumeId;
    std::string executablePath;
    std::string executableSha1;
    std::string version; // the decomp's version id ("us", "eu"...), empty when the executable is not known
};

// The disc, as the extractors see it. It refers to objects that must outlive it.
class Disc {
public:
    Disc(formats::disc::SectorSource& source, const formats::disc::IsoVolume& volume, const DiscInfo& info,
         const GameTables* tables = nullptr)
        : source_(&source), volume_(&volume), info_(&info), tables_(tables) {}

    [[nodiscard]] formats::disc::SectorSource& source() const { return *source_; }
    [[nodiscard]] const formats::disc::IsoVolume& volume() const { return *volume_; }
    [[nodiscard]] const DiscInfo& info() const { return *info_; }
    // The executable's tables, or nullptr when the version or its layout is not known.
    [[nodiscard]] const GameTables* tables() const { return tables_; }

    // Returns the file with this volume path ("MAP/MAP1/NAME.MAP"), or nullptr.
    [[nodiscard]] const formats::disc::IsoFile* findFile(std::string_view path) const {
        const auto& files = volume_->files;
        const auto found = std::ranges::lower_bound(files, path, std::less<>{}, &formats::disc::IsoFile::path);
        return found != files.end() && found->path == path ? &*found : nullptr;
    }

private:
    formats::disc::SectorSource* source_;
    const formats::disc::IsoVolume* volume_;
    const DiscInfo* info_;
    const GameTables* tables_;
};

// Turns disc files into source assets. A new kind of file is one more entry in the registry (extractors.cpp) and
// the two functions it names. Several files are extracted at once on different threads, so an extractor keeps no
// state between calls and reads the disc only through the Disc it is given.
struct Extractor {
    std::string_view name;
    // Whether it takes this file, by its path on the volume ("CHDAT/ALLTIM.TIM").
    bool (*handles)(std::string_view path);
    core::Result<Extraction> (*extract)(const Disc& disc, const formats::disc::IsoFile& file);
};

// Returns every extractor. The first one that handles a file takes it, so specific entries go before patterns.
std::span<const Extractor> extractors();

// Returns the extractor that takes a file, or nullptr when the file is still pending.
const Extractor* findExtractor(std::span<const Extractor> registry, std::string_view path);

} // namespace dwe::dwx
