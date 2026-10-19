#include <dwe/formats/byte_reader.hpp>
#include <dwe/formats/disc/iso9660.hpp>

#include <algorithm>
#include <format>
#include <set>
#include <string_view>

namespace dwe::formats::disc {

namespace {

// ECMA-119 (ISO9660) layout. The primary volume descriptor is the first descriptor, right after the 16 system sectors.
constexpr std::uint32_t PrimaryVolumeDescriptorLba = 16;
constexpr std::uint8_t PrimaryVolumeDescriptorType = 1;
constexpr std::string_view StandardIdentifier = "CD001";
constexpr std::size_t VolumeIdOffset = 40;
constexpr std::size_t VolumeIdSize = 32;
constexpr std::size_t VolumeSpaceSizeOffset = 80;
constexpr std::size_t LogicalBlockSizeOffset = 128;
constexpr std::size_t RootRecordOffset = 156;

// Directory record fields. The numbers are "both-endian"; the little-endian half comes first.
constexpr std::size_t MinRecordSize = 33;
constexpr std::size_t RecordExtendedAttributesOffset = 1;
constexpr std::size_t RecordLbaOffset = 2;
constexpr std::size_t RecordSizeOffset = 10;
constexpr std::size_t RecordFlagsOffset = 25;
constexpr std::size_t RecordUnitSizeOffset = 26;
constexpr std::size_t RecordGapOffset = 27;
constexpr std::size_t RecordNameLengthOffset = 32;
constexpr std::size_t RecordNameOffset = 33;
constexpr std::uint8_t FlagDirectory = 0x02;
constexpr std::uint8_t FlagMultiExtent = 0x80;

// Limits that no real disc reaches; they keep a hostile image from making the walk run away.
constexpr std::size_t MaxDepth = 32;
constexpr std::size_t MaxFiles = 1U << 16U;

struct DirectoryRecord {
    std::uint32_t lba;
    std::uint32_t size;
    bool isDirectory;
    std::string_view name; // raw: with the ";1" version, or "\0" and "\1" for the folder itself and its parent
};

struct PendingDirectory {
    std::uint32_t lba;
    std::uint32_t size;
    std::string path;
    std::size_t depth;
};

std::uint32_t sectorsFor(std::uint32_t size) {
    return static_cast<std::uint32_t>((std::uint64_t{size} + Form1DataSize - 1) / Form1DataSize);
}

core::Result<DirectoryRecord> parseRecord(const ByteReader& record) {
    const auto extendedAttributes = record.u8(RecordExtendedAttributesOffset);
    const auto lba = record.u32(RecordLbaOffset);
    const auto size = record.u32(RecordSizeOffset);
    const auto flags = record.u8(RecordFlagsOffset);
    const auto unitSize = record.u8(RecordUnitSizeOffset);
    const auto gap = record.u8(RecordGapOffset);
    const auto nameLength = record.u8(RecordNameLengthOffset);
    if (!extendedAttributes || !lba || !size || !flags || !unitSize || !gap || !nameLength) {
        return core::fail("truncated directory record");
    }
    const auto name = record.chars(RecordNameOffset, *nameLength);
    if (!name || *nameLength == 0) {
        return core::fail("directory record name does not fit in the record");
    }
    if (*extendedAttributes != 0 || (*flags & FlagMultiExtent) != 0 || *unitSize != 0 || *gap != 0) {
        return core::fail(std::format("unsupported directory record (extended attributes, multi-extent or "
                                      "interleaved file) for {}",
                                      *name));
    }
    return DirectoryRecord{.lba = *lba, .size = *size, .isDirectory = (*flags & FlagDirectory) != 0, .name = *name};
}

bool isSelfOrParent(std::string_view name) {
    return name.size() == 1 && (name[0] == '\0' || name[0] == '\1');
}

// Removes the ";1" version and the dot of a name without extension, and rejects names that could escape a folder.
core::Result<std::string> cleanName(std::string_view name) {
    if (const auto semicolon = name.find(';'); semicolon != std::string_view::npos) {
        name = name.substr(0, semicolon);
    }
    if (name.ends_with('.')) {
        name.remove_suffix(1);
    }
    const bool unsafe = std::ranges::any_of(name, [](char c) {
        const auto code = static_cast<unsigned char>(c);
        return c == '/' || c == '\\' || code < 0x20 || code == 0x7F;
    });
    if (name.empty() || name == "." || name == ".." || unsafe) {
        return core::fail("invalid file name in a directory record");
    }
    return std::string{name};
}

core::Result<std::span<const std::byte>> readForm1Sector(SectorSource& source, std::uint32_t lba) {
    const auto sector = source.readSector(lba);
    if (!sector) {
        return std::unexpected{sector.error()};
    }
    if (sector->form != SectorForm::Form1) {
        return core::fail(std::format("sector {} is a form 2 sector where form 1 data was expected", lba));
    }
    return sector->userData;
}

bool fitsInImage(const SectorSource& source, std::uint32_t lba, std::uint32_t sectors) {
    return std::uint64_t{lba} + sectors <= source.sectorCount();
}

// Reads one directory: files go to the volume, folders to the pending list.
core::Result<void> readDirectory(SectorSource& source, const PendingDirectory& directory, IsoVolume& volume,
                                 std::vector<PendingDirectory>& pending) {
    const std::uint32_t sectors = sectorsFor(directory.size);
    if (sectors == 0 || !fitsInImage(source, directory.lba, sectors)) {
        return core::fail(
            std::format("directory /{} at sector {} lies outside the image", directory.path, directory.lba));
    }
    for (std::uint32_t i = 0; i < sectors; ++i) {
        const auto data = readForm1Sector(source, directory.lba + i);
        if (!data) {
            return std::unexpected{data.error()};
        }
        const ByteReader reader{*data};
        std::size_t offset = 0;
        while (offset < reader.size()) {
            const auto length = reader.u8(offset);
            if (!length || *length == 0) {
                break; // records never cross a sector: the rest of this one is padding
            }
            const auto bytes = reader.bytes(offset, *length);
            if (*length < MinRecordSize || !bytes) {
                return core::fail(std::format("directory /{}: bad record at sector {} offset {}", directory.path,
                                              directory.lba + i, offset));
            }
            offset += *length;

            const auto record = parseRecord(ByteReader{*bytes});
            if (!record) {
                return core::fail(std::format("directory /{}: {}", directory.path, record.error().message));
            }
            if (isSelfOrParent(record->name)) {
                continue;
            }
            const auto name = cleanName(record->name);
            if (!name) {
                return core::fail(std::format("directory /{}: {}", directory.path, name.error().message));
            }
            std::string path = directory.path.empty() ? *name : std::format("{}/{}", directory.path, *name);
            if (record->isDirectory) {
                pending.push_back(PendingDirectory{
                    .lba = record->lba, .size = record->size, .path = std::move(path), .depth = directory.depth + 1});
                continue;
            }
            const IsoFile file{.path = std::move(path), .lba = record->lba, .size = record->size};
            if (!fitsInImage(source, file.lba, sectorSpan(file))) {
                return core::fail(
                    std::format("{} at sector {} ({} bytes) lies outside the image", file.path, file.lba, file.size));
            }
            if (volume.files.size() >= MaxFiles) {
                return core::fail("too many files in the volume");
            }
            volume.files.push_back(file);
        }
    }
    return {};
}

} // namespace

std::uint32_t sectorSpan(const IsoFile& file) {
    return sectorsFor(file.size);
}

core::Result<IsoVolume> readIsoVolume(SectorSource& source) {
    const auto descriptor = readForm1Sector(source, PrimaryVolumeDescriptorLba);
    if (!descriptor) {
        return core::fail(std::format("no primary volume descriptor: {}", descriptor.error().message));
    }
    const ByteReader reader{*descriptor};
    const auto type = reader.u8(0);
    const auto identifier = reader.chars(1, StandardIdentifier.size());
    const auto volumeId = reader.chars(VolumeIdOffset, VolumeIdSize);
    const auto volumeSectors = reader.u32(VolumeSpaceSizeOffset);
    const auto blockSize = reader.u16(LogicalBlockSizeOffset);
    const auto rootBytes = reader.bytes(RootRecordOffset, MinRecordSize + 1);
    if (!type || !identifier || !volumeId || !volumeSectors || !blockSize || !rootBytes) {
        return core::fail("truncated primary volume descriptor");
    }
    if (*type != PrimaryVolumeDescriptorType || *identifier != StandardIdentifier) {
        return core::fail("sector 16 is not an ISO9660 primary volume descriptor");
    }
    if (*blockSize != Form1DataSize) {
        return core::fail(std::format("unsupported logical block size {}", *blockSize));
    }
    const auto root = parseRecord(ByteReader{*rootBytes});
    if (!root || !root->isDirectory) {
        return core::fail("the root directory record is invalid");
    }

    IsoVolume volume{.volumeId = std::string{*volumeId}, .volumeSectors = *volumeSectors, .files = {}};
    volume.volumeId.erase(volume.volumeId.find_last_not_of(' ') + 1);

    std::vector<PendingDirectory> pending{{.lba = root->lba, .size = root->size, .path = {}, .depth = 0}};
    std::set<std::uint32_t> visited;
    while (!pending.empty()) {
        const PendingDirectory directory = std::move(pending.back());
        pending.pop_back();
        if (!visited.insert(directory.lba).second) {
            return core::fail(
                std::format("directory loop: /{} points back to sector {}", directory.path, directory.lba));
        }
        if (directory.depth > MaxDepth) {
            return core::fail(std::format("directory /{} is nested too deep", directory.path));
        }
        if (const auto read = readDirectory(source, directory, volume, pending); !read) {
            return std::unexpected{read.error()};
        }
    }

    std::ranges::sort(volume.files, {}, &IsoFile::path);
    const auto duplicate = std::ranges::adjacent_find(volume.files, {}, &IsoFile::path);
    if (duplicate != volume.files.end()) {
        return core::fail(std::format("{} appears twice in the volume", duplicate->path));
    }
    return volume;
}

core::Result<std::vector<std::byte>> readIsoFile(SectorSource& source, const IsoFile& file) {
    std::vector<std::byte> bytes;
    bytes.reserve(file.size);
    const std::uint32_t sectors = sectorSpan(file);
    for (std::uint32_t i = 0; i < sectors; ++i) {
        const auto data = readForm1Sector(source, file.lba + i);
        if (!data) {
            return core::fail(std::format("{}: {}", file.path, data.error().message));
        }
        const std::size_t count = std::min(data->size(), std::size_t{file.size} - bytes.size());
        bytes.insert(bytes.end(), data->begin(), data->begin() + static_cast<std::ptrdiff_t>(count));
    }
    return bytes;
}

core::Result<core::Sha1Digest> hashIsoFile(SectorSource& source, const IsoFile& file) {
    core::Sha1 hasher;
    std::size_t remaining = file.size;
    const std::uint32_t sectors = sectorSpan(file);
    for (std::uint32_t i = 0; i < sectors; ++i) {
        const auto sector = source.readSector(file.lba + i);
        if (!sector) {
            return core::fail(std::format("{}: {}", file.path, sector.error().message));
        }
        if (sector->form == SectorForm::Form2) {
            hasher.update(sector->userData);
        } else {
            hasher.update(sector->userData.first(std::min(sector->userData.size(), remaining)));
        }
        remaining -= std::min(remaining, Form1DataSize);
    }
    return hasher.finish();
}

} // namespace dwe::formats::disc
