#pragma once

#include <dwe/core/error.hpp>
#include <dwe/core/sha1.hpp>
#include <dwe/formats/disc/sector.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace dwe::formats::disc {

// A file of an ISO9660 volume.
struct IsoFile {
  std::string path; // folders separated by '/', without the ";1" version: "MAP/MAP1/NAME.MAP"
  std::uint32_t lba;
  std::uint32_t size; // in bytes, as the directory record says

  bool operator==(const IsoFile&) const = default;
};

// The files of an ISO9660 volume, sorted by path.
struct IsoVolume {
  std::string volumeId;
  std::uint32_t volumeSectors;
  std::vector<IsoFile> files;
};

// Reads the primary volume descriptor and walks every directory. Directory loops, records that
// cross their sector, names with path separators and extents past the end of the image are errors.
core::Result<IsoVolume> readIsoVolume(SectorSource& source);

// Returns the number of sectors a file spans.
std::uint32_t sectorSpan(const IsoFile& file);

// Reads a file made of form 1 sectors (any file except video and audio streams).
core::Result<std::vector<std::byte>> readIsoFile(SectorSource& source, const IsoFile& file);

// Hashes the user data of a file's sectors, each with its own form: a form 1 sector adds its 2048
// bytes (the last one cut to the file size) and a form 2 sector its 2324 bytes. For a file of form
// 1 sectors this is the digest of its bytes; video files with interleaved audio, whose size counts
// 2048 bytes per sector, are hashed whole.
core::Result<core::Sha1Digest> hashIsoFile(SectorSource& source, const IsoFile& file);

} // namespace dwe::formats::disc
