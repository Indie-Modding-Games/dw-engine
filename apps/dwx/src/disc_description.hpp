#pragma once

#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/disc/sector.hpp>

#include <optional>
#include <string>
#include <vector>

#include "extractor.hpp"
#include "game_tables.hpp"

namespace dwe::dwx {

// What the extraction knows about a disc before it looks at its files.
struct DiscDescription {
    DiscInfo info;
    std::optional<GameTables> tables;
    std::vector<std::string> warnings;
};

// Identifies the version by the boot executable and reads its tables. Nothing here stops an extraction: an unknown
// executable leaves the version empty and the tables unread, so assets are named by number, and says why in the
// warnings.
DiscDescription describeDisc(formats::disc::SectorSource& source, const formats::disc::IsoVolume& volume);

} // namespace dwe::dwx
