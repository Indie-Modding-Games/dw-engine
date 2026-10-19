#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/disc/iso9660.hpp>

#include <string>
#include <string_view>

namespace dwe::formats::disc {

// Returns the volume path of the executable a SYSTEM.CNF boots ("BOOT = cdrom:\NAME.EXE;1" gives "NAME.EXE").
core::Result<std::string> parseBootPath(std::string_view systemCnf);

// Finds the executable the disc boots, through the SYSTEM.CNF at the root of the volume.
core::Result<IsoFile> findBootExecutable(SectorSource& source, const IsoVolume& volume);

} // namespace dwe::formats::disc
