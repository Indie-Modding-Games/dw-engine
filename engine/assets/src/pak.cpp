#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>

namespace dwe::assets {

namespace {

// PNG-style: a byte above 0x7F and both line endings, so a transfer that mangles binary files breaks the magic.
constexpr std::array<std::byte, 8> Magic = {std::byte{0x89}, std::byte{'P'},  std::byte{'A'},  std::byte{'K'},
                                            std::byte{0x0D}, std::byte{0x0A}, std::byte{0x1A}, std::byte{0x0A}};
constexpr std::string_view ManifestName = "pack.yaml";
constexpr std::uint32_t CompressionMask = 0xFF;

struct Header {
    std::uint32_t version = 0;
    std::uint32_t flags = 0;
    std::uint32_t entryCount = 0;
    std::uint32_t alignment = 0;
    std::uint64_t tocOffset = 0;
    std::uint64_t namesOffset = 0;
    std::uint64_t namesSize = 0;
    std::uint64_t manifestOffset = 0;
    std::uint64_t manifestSize = 0;
};

// Little-endian fields of a buffer whose size the caller has checked.
template <typename T>
T fieldAt(std::span<const std::byte> bytes, std::size_t offset) {
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value = static_cast<T>(value | (static_cast<T>(bytes[offset + i]) << (8U * i)));
    }
    return value;
}

template <typename T>
void putField(std::span<std::byte> bytes, std::size_t offset, T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        bytes[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
    }
}

std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

std::string_view textOf(std::span<const std::byte> bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::string normalized(const std::filesystem::path& path) {
    std::string text = pathText(path.lexically_normal());
    while (text.size() > 1 && text.ends_with('/')) {
        text.pop_back();
    }
    return text;
}

// Reads count bytes at offset of an open file.
core::Result<std::vector<std::byte>> readAt(std::ifstream& stream, const std::filesystem::path& archive,
                                            std::uint64_t offset, std::uint64_t count) {
    std::vector<std::byte> bytes(static_cast<std::size_t>(count));
    stream.seekg(static_cast<std::streamoff>(offset));
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream || stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return core::fail(std::format("cannot read '{}': it ends early", pathText(archive)));
    }
    return bytes;
}

Header parseHeader(std::span<const std::byte, PakHeaderSize> bytes) {
    return Header{.version = fieldAt<std::uint32_t>(bytes, 0x08),
                  .flags = fieldAt<std::uint32_t>(bytes, 0x0C),
                  .entryCount = fieldAt<std::uint32_t>(bytes, 0x10),
                  .alignment = fieldAt<std::uint32_t>(bytes, 0x14),
                  .tocOffset = fieldAt<std::uint64_t>(bytes, 0x18),
                  .namesOffset = fieldAt<std::uint64_t>(bytes, 0x20),
                  .namesSize = fieldAt<std::uint64_t>(bytes, 0x28),
                  .manifestOffset = fieldAt<std::uint64_t>(bytes, 0x30),
                  .manifestSize = fieldAt<std::uint64_t>(bytes, 0x38)};
}

// Checks the header against the file's size. The index (table of contents, names, manifest) runs without gaps from
// the table of contents to the end of the file, so the size bounds every allocation that follows.
std::optional<std::string> headerProblem(const Header& header, std::uint64_t fileSize) {
    if (header.version != PakVersion) {
        return std::format("version {} is not supported (this build reads version {})", header.version, PakVersion);
    }
    if (header.flags != 0) {
        return std::format("unknown header flags {:#x}", header.flags);
    }
    if (header.alignment < PakAlignment || (header.alignment & (header.alignment - 1)) != 0) {
        return std::format("the alignment {} is not a power of two of at least {}", header.alignment, PakAlignment);
    }
    if (header.tocOffset < PakHeaderSize || header.tocOffset % header.alignment != 0 || header.tocOffset > fileSize) {
        return std::format("the table of contents at {} is out of place", header.tocOffset);
    }
    const std::uint64_t tocSize = std::uint64_t{header.entryCount} * PakEntrySize;
    if (tocSize > fileSize - header.tocOffset || header.namesOffset != header.tocOffset + tocSize) {
        return "the names do not follow the table of contents";
    }
    if (header.namesSize > fileSize - header.namesOffset ||
        header.manifestOffset != header.namesOffset + header.namesSize) {
        return "the manifest does not follow the names";
    }
    if (header.manifestSize != fileSize - header.manifestOffset) {
        return "the manifest does not end the file";
    }
    return std::nullopt;
}

// Reads the table of contents from the index (the bytes from the table of contents to the end of the file).
core::Result<std::vector<PakEntry>> parseEntries(const Header& header, std::span<const std::byte> index) {
    const auto names = index.subspan(header.namesOffset - header.tocOffset, header.namesSize);
    std::vector<PakEntry> entries;
    entries.reserve(header.entryCount);
    std::optional<AssetId> previous;
    for (std::size_t i = 0; i < header.entryCount; ++i) {
        const auto entry = index.subspan(i * PakEntrySize, PakEntrySize);
        const auto offset = fieldAt<std::uint64_t>(entry, 0x00);
        const auto storedSize = fieldAt<std::uint64_t>(entry, 0x08);
        const auto size = fieldAt<std::uint64_t>(entry, 0x10);
        const auto nameOffset = fieldAt<std::uint32_t>(entry, 0x18);
        const auto nameSize = fieldAt<std::uint32_t>(entry, 0x1C);
        const auto flags = fieldAt<std::uint32_t>(entry, 0x20);
        const auto reserved = fieldAt<std::uint64_t>(entry, 0x38);
        if ((flags & CompressionMask) != static_cast<std::uint32_t>(PakCompression::None)) {
            return core::fail(std::format("entry {} uses compression {}, which this build does not read", i,
                                          flags & CompressionMask));
        }
        if ((flags & ~CompressionMask) != 0 || reserved != 0) {
            return core::fail(std::format("entry {} has unknown flags", i));
        }
        if (storedSize != size) {
            return core::fail(std::format("entry {} is stored uncompressed but its sizes differ", i));
        }
        if (offset < PakHeaderSize || offset % header.alignment != 0 || offset > header.tocOffset ||
            size > header.tocOffset - offset) {
            return core::fail(std::format("entry {} lies outside the data", i));
        }
        if (std::uint64_t{nameOffset} + nameSize > names.size()) {
            return core::fail(std::format("the name of entry {} lies outside the names", i));
        }
        std::string path{textOf(names.subspan(nameOffset, nameSize))};
        auto id = assetIdOfFile(path);
        if (!id || path == ManifestName) {
            return core::fail(std::format("entry {} has the invalid name '{}'", i, path));
        }
        if (previous && !(*previous < *id)) {
            return core::fail(std::format("entry {} ('{}') is out of order or repeats an asset id", i, path));
        }
        previous = std::move(*id);
        PakEntry& added = entries.emplace_back(PakEntry{.path = std::move(path), .offset = offset, .size = size});
        std::ranges::copy(entry.subspan(0x24, added.sha1.size()), added.sha1.begin());
    }
    return entries;
}

std::vector<std::byte> headerBytes(const Header& header) {
    std::vector<std::byte> bytes(PakHeaderSize);
    std::ranges::copy(Magic, bytes.begin());
    putField(bytes, 0x08, header.version);
    putField(bytes, 0x0C, header.flags);
    putField(bytes, 0x10, header.entryCount);
    putField(bytes, 0x14, header.alignment);
    putField(bytes, 0x18, header.tocOffset);
    putField(bytes, 0x20, header.namesOffset);
    putField(bytes, 0x28, header.namesSize);
    putField(bytes, 0x30, header.manifestOffset);
    putField(bytes, 0x38, header.manifestSize);
    return bytes;
}

// The table of contents, the names and the manifest, in that order.
std::vector<std::byte> indexBytes(std::span<const PakEntry> entries, std::span<const std::byte> manifest) {
    std::vector<std::byte> index(entries.size() * PakEntrySize);
    std::uint32_t nameOffset = 0;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const PakEntry& entry = entries[i];
        const auto bytes = std::span{index}.subspan(i * PakEntrySize, PakEntrySize);
        putField(bytes, 0x00, entry.offset);
        putField(bytes, 0x08, entry.size);
        putField(bytes, 0x10, entry.size);
        putField(bytes, 0x18, nameOffset);
        putField(bytes, 0x1C, static_cast<std::uint32_t>(entry.path.size()));
        putField(bytes, 0x20, static_cast<std::uint32_t>(PakCompression::None));
        std::ranges::copy(entry.sha1, bytes.begin() + 0x24);
        nameOffset += static_cast<std::uint32_t>(entry.path.size());
    }
    for (const PakEntry& entry : entries) {
        const auto name = std::as_bytes(std::span{entry.path});
        index.insert(index.end(), name.begin(), name.end());
    }
    index.insert(index.end(), manifest.begin(), manifest.end());
    return index;
}

void writeBytes(std::ofstream& stream, std::span<const std::byte> bytes) {
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

core::Result<std::unique_ptr<PakFileSystem>> PakFileSystem::open(const std::filesystem::path& archive) {
    const auto failure = [&](std::string_view problem) {
        return core::fail(std::format("'{}' is not a valid .pak: {}", pathText(archive), problem));
    };
    std::error_code error;
    const std::uint64_t fileSize = std::filesystem::file_size(archive, error);
    if (error) {
        return core::fail(std::format("cannot open '{}': {}", pathText(archive), error.message()));
    }
    if (fileSize < PakHeaderSize) {
        return failure("it is shorter than its header");
    }
    std::ifstream stream{archive, std::ios::binary};
    if (!stream) {
        return core::fail(std::format("cannot open '{}'", pathText(archive)));
    }
    const auto headerRead = readAt(stream, archive, 0, PakHeaderSize);
    if (!headerRead) {
        return std::unexpected{headerRead.error()};
    }
    const std::span<const std::byte, PakHeaderSize> bytes{headerRead->data(), PakHeaderSize};
    if (!std::ranges::equal(bytes.first<Magic.size()>(), Magic)) {
        return failure("it does not start with the .pak magic");
    }
    const Header header = parseHeader(bytes);
    if (const auto problem = headerProblem(header, fileSize)) {
        return failure(*problem);
    }
    const auto index = readAt(stream, archive, header.tocOffset, fileSize - header.tocOffset);
    if (!index) {
        return std::unexpected{index.error()};
    }
    auto entries = parseEntries(header, *index);
    if (!entries) {
        return failure(entries.error().message);
    }
    const auto manifestStart = index->begin() + static_cast<std::ptrdiff_t>(header.manifestOffset - header.tocOffset);
    std::vector<std::byte> manifest{manifestStart, index->end()};
    return std::make_unique<PakFileSystem>(OpenKey{}, archive, std::move(manifest), std::move(*entries));
}

core::Result<std::vector<std::string>> PakFileSystem::listFiles(const std::filesystem::path& folder) const {
    if (normalized(folder) != normalized(archive_)) {
        return core::fail(std::format("cannot open folder '{}': the archive '{}' only has its own root",
                                      pathText(folder), pathText(archive_)));
    }
    std::vector<std::string> files{std::string{ManifestName}};
    for (const PakEntry& entry : entries_) {
        files.push_back(entry.path);
    }
    return files;
}

core::Result<std::vector<std::byte>> PakFileSystem::readFile(const std::filesystem::path& file) const {
    const std::string archive = normalized(archive_);
    const std::string path = normalized(file);
    if (!path.starts_with(archive) || path.size() <= archive.size() + 1 || path[archive.size()] != '/') {
        return core::fail(std::format("cannot read '{}': it is not in the archive '{}'", pathText(file), archive));
    }
    const std::string_view name = std::string_view{path}.substr(archive.size() + 1);
    if (name == ManifestName) {
        return manifest_;
    }
    // Sorted by asset id is also sorted by path: '.' sorts before every character an id may have.
    const auto entry = std::ranges::lower_bound(entries_, name, {}, &PakEntry::path);
    if (entry == entries_.end() || entry->path != name) {
        return core::fail(std::format("cannot read '{}': it does not exist", path));
    }
    std::ifstream stream{archive_, std::ios::binary};
    auto bytes = readAt(stream, archive_, entry->offset, entry->size);
    if (!bytes) {
        return bytes;
    }
    if (core::sha1(*bytes) != entry->sha1) {
        return core::fail(
            std::format("cannot read '{}': its bytes do not match their SHA-1; the archive is damaged", path));
    }
    return bytes;
}

core::Result<std::vector<PakEntry>> writePak(const FileSystem& fileSystem, const std::filesystem::path& packRoot,
                                             const std::filesystem::path& archive) {
    const std::filesystem::path manifestPath = packRoot / ManifestName;
    const auto manifest = fileSystem.readFile(manifestPath);
    if (!manifest) {
        return std::unexpected{manifest.error()};
    }
    const auto parsed = parsePackManifest(textOf(*manifest));
    if (!parsed) {
        return core::fail(std::format("{}: {}", pathText(manifestPath), parsed.error().message));
    }
    const auto files = listPackFiles(fileSystem, packRoot);
    if (!files) {
        return core::fail(std::format("pack '{}': {}", parsed->id, files.error().message));
    }
    if (files->size() > std::numeric_limits<std::uint32_t>::max()) {
        return core::fail(std::format("pack '{}' has more files than a .pak holds", parsed->id));
    }

    std::ofstream stream{archive, std::ios::binary | std::ios::trunc};
    if (!stream) {
        return core::fail(std::format("cannot create '{}'", pathText(archive)));
    }
    const std::array<std::byte, PakAlignment> zeros{};
    std::uint64_t position = PakHeaderSize;
    writeBytes(stream, std::vector<std::byte>(PakHeaderSize));
    std::vector<PakEntry> entries;
    entries.reserve(files->size());
    for (const PackFile& file : *files) {
        const auto bytes = fileSystem.readFile(packRoot / file.path);
        if (!bytes) {
            return std::unexpected{bytes.error()};
        }
        const std::uint64_t offset = alignUp(position, PakAlignment);
        writeBytes(stream, std::span{zeros}.first(static_cast<std::size_t>(offset - position)));
        writeBytes(stream, *bytes);
        position = offset + bytes->size();
        entries.push_back(
            PakEntry{.path = file.path, .offset = offset, .size = bytes->size(), .sha1 = core::sha1(*bytes)});
    }
    const std::uint64_t tocOffset = alignUp(position, PakAlignment);
    writeBytes(stream, std::span{zeros}.first(static_cast<std::size_t>(tocOffset - position)));
    std::uint64_t namesSize = 0;
    for (const PakEntry& entry : entries) {
        namesSize += entry.path.size();
    }
    if (namesSize > std::numeric_limits<std::uint32_t>::max()) {
        return core::fail(std::format("the file names of pack '{}' do not fit in a .pak", parsed->id));
    }
    writeBytes(stream, indexBytes(entries, *manifest));
    const std::uint64_t namesOffset = tocOffset + (entries.size() * PakEntrySize);
    const Header header{.version = PakVersion,
                        .entryCount = static_cast<std::uint32_t>(entries.size()),
                        .alignment = PakAlignment,
                        .tocOffset = tocOffset,
                        .namesOffset = namesOffset,
                        .namesSize = namesSize,
                        .manifestOffset = namesOffset + namesSize,
                        .manifestSize = manifest->size()};
    stream.seekp(0);
    writeBytes(stream, headerBytes(header));
    stream.close();
    if (!stream) {
        return core::fail(std::format("cannot write '{}'", pathText(archive)));
    }
    return entries;
}

} // namespace dwe::assets
