#include <dwe/assets/asset_id.hpp>
#include <dwe/assets/pack_manifest.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>

#include <zstd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <format>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace dwe::assets {

namespace {

// PNG-style: a byte above 0x7F and both line endings, so a transfer that mangles binary files breaks the magic.
constexpr std::array<std::byte, 8> Magic = {std::byte{0x89}, std::byte{'P'},  std::byte{'A'},  std::byte{'K'},
                                            std::byte{0x0D}, std::byte{0x0A}, std::byte{0x1A}, std::byte{0x0A}};
constexpr std::string_view ManifestName = "pack.yaml";
constexpr std::uint32_t CompressionMask = 0xFF;
// How many files writePak holds in memory at once, read and compressed in parallel and then written in order.
constexpr std::size_t WriteBatch = 64;
constexpr unsigned MaxThreads = 16;

// Runs task(0) to task(count - 1) on several threads and returns when all are done.
template <typename Task>
void runInParallel(std::size_t count, const Task& task) {
    std::atomic<std::size_t> next{0};
    const auto worker = [&] {
        for (std::size_t index = next++; index < count; index = next++) {
            task(index);
        }
    };
    const unsigned threadCount = static_cast<unsigned>(std::min<std::size_t>(
        std::clamp(std::thread::hardware_concurrency(), 1U, MaxThreads), std::max<std::size_t>(count, 1)));
    std::vector<std::jthread> threads;
    for (unsigned i = 1; i < threadCount; ++i) {
        threads.emplace_back(worker);
    }
    worker();
}

struct FreeCompressor {
    void operator()(ZSTD_CCtx* context) const { ZSTD_freeCCtx(context); }
};

// Compresses a file into one zstd frame with fixed settings: the level, the content size in the frame header, and
// no checksum (the entry's SHA-1 covers it) or dictionary id. Returns nothing when that does not make it smaller.
core::Result<std::optional<std::vector<std::byte>>> compress(std::span<const std::byte> bytes) {
    const std::unique_ptr<ZSTD_CCtx, FreeCompressor> context{ZSTD_createCCtx()};
    if (!context) {
        return core::fail("cannot create a zstd context");
    }
    for (const auto [parameter, value] :
         {std::pair{ZSTD_c_compressionLevel, PakZstdLevel}, std::pair{ZSTD_c_contentSizeFlag, 1},
          std::pair{ZSTD_c_checksumFlag, 0}, std::pair{ZSTD_c_dictIDFlag, 0}, std::pair{ZSTD_c_nbWorkers, 0}}) {
        if (const std::size_t result = ZSTD_CCtx_setParameter(context.get(), parameter, value);
            ZSTD_isError(result) != 0) {
            return core::fail(std::format("cannot set up zstd: {}", ZSTD_getErrorName(result)));
        }
    }
    std::vector<std::byte> compressed(ZSTD_compressBound(bytes.size()));
    const std::size_t size =
        ZSTD_compress2(context.get(), compressed.data(), compressed.size(), bytes.data(), bytes.size());
    if (ZSTD_isError(size) != 0) {
        return core::fail(std::format("zstd cannot compress: {}", ZSTD_getErrorName(size)));
    }
    if (size >= bytes.size()) {
        return std::nullopt;
    }
    compressed.resize(size);
    return std::optional{std::move(compressed)};
}

struct FreeDecompressor {
    void operator()(ZSTD_DCtx* context) const { ZSTD_freeDCtx(context); }
};

// The first output buffer of a decompression, which then doubles as needed, so that what is allocated follows what
// the frame really expands to, not the size an archive claims.
constexpr std::size_t FirstOutputChunk = std::size_t{1} << 20U;

// Expands one zstd frame that must hold exactly size bytes and nothing after it.
core::Result<std::vector<std::byte>> decompress(std::span<const std::byte> stored, std::uint64_t size) {
    if (ZSTD_getFrameContentSize(stored.data(), stored.size()) != size ||
        ZSTD_findFrameCompressedSize(stored.data(), stored.size()) != stored.size()) {
        return core::fail("it is not one zstd frame of its size");
    }
    const std::unique_ptr<ZSTD_DCtx, FreeDecompressor> context{ZSTD_createDCtx()};
    if (!context) {
        return core::fail("cannot create a zstd context");
    }
    const auto total = static_cast<std::size_t>(size);
    std::vector<std::byte> bytes(std::min(total, FirstOutputChunk));
    ZSTD_inBuffer input{.src = stored.data(), .size = stored.size(), .pos = 0};
    std::size_t produced = 0;
    for (;;) {
        ZSTD_outBuffer output{.dst = bytes.data(), .size = bytes.size(), .pos = produced};
        const std::size_t consumed = input.pos;
        const std::size_t result = ZSTD_decompressStream(context.get(), &output, &input);
        if (ZSTD_isError(result) != 0) {
            return core::fail(std::format("zstd cannot decompress it: {}", ZSTD_getErrorName(result)));
        }
        produced = output.pos;
        if (result == 0) {
            break;
        }
        if (produced < bytes.size()) {
            if (input.pos == input.size) {
                return core::fail("its zstd frame ends early");
            }
        } else if (bytes.size() < total) {
            bytes.resize(bytes.size() + std::min(total - bytes.size(), bytes.size()));
        } else if (input.pos == consumed) {
            // Full, and nothing more read: the frame holds more than its size.
            return core::fail("it decompresses to more than its size");
        }
    }
    if (produced != total || input.pos != input.size) {
        return core::fail("it decompresses to the wrong size");
    }
    return bytes;
}

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
    if (header.version < PakOldestVersion || header.version > PakVersion) {
        return std::format("version {} is not supported (this build reads versions {} to {})", header.version,
                           PakOldestVersion, PakVersion);
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
        const std::uint32_t compression = flags & CompressionMask;
        const bool zstd = compression == static_cast<std::uint32_t>(PakCompression::Zstd) && header.version >= 2;
        if (compression != static_cast<std::uint32_t>(PakCompression::None) && !zstd) {
            return core::fail(std::format("entry {} uses compression {}, which version {} does not have", i,
                                          compression, header.version));
        }
        if ((flags & ~CompressionMask) != 0 || reserved != 0) {
            return core::fail(std::format("entry {} has unknown flags", i));
        }
        if (!zstd && storedSize != size) {
            return core::fail(std::format("entry {} is stored uncompressed but its sizes differ", i));
        }
        if (zstd && size > PakMaxEntrySize) {
            return core::fail(std::format("entry {} expands to {} bytes, more than a .pak entry may", i, size));
        }
        if (offset < PakHeaderSize || offset % header.alignment != 0 || offset > header.tocOffset ||
            storedSize > header.tocOffset - offset) {
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
        PakEntry& added =
            entries.emplace_back(PakEntry{.path = std::move(path),
                                          .offset = offset,
                                          .size = size,
                                          .storedSize = storedSize,
                                          .compression = zstd ? PakCompression::Zstd : PakCompression::None,
                                          .sha1 = {}});
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
        putField(bytes, 0x08, entry.storedSize);
        putField(bytes, 0x10, entry.size);
        putField(bytes, 0x18, nameOffset);
        putField(bytes, 0x1C, static_cast<std::uint32_t>(entry.path.size()));
        putField(bytes, 0x20, static_cast<std::uint32_t>(entry.compression));
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
    std::vector<std::string> files;
    files.reserve(entries_.size() + 1);
    files.emplace_back(ManifestName);
    for (const PakEntry& entry : entries_) {
        files.push_back(entry.path);
    }
    return files;
}

core::Result<const PakEntry*> PakFileSystem::entryOf(const std::filesystem::path& file) const {
    const std::string archive = normalized(archive_);
    const std::string path = normalized(file);
    if (!path.starts_with(archive) || path.size() <= archive.size() + 1 || path[archive.size()] != '/') {
        return core::fail(std::format("cannot read '{}': it is not in the archive '{}'", pathText(file), archive));
    }
    const std::string_view name = std::string_view{path}.substr(archive.size() + 1);
    if (name == ManifestName) {
        return nullptr;
    }
    // Sorted by asset id is also sorted by path: '.' sorts before every character an id may have.
    const auto entry = std::ranges::lower_bound(entries_, name, {}, &PakEntry::path);
    if (entry == entries_.end() || entry->path != name) {
        return core::fail(std::format("cannot read '{}': it does not exist", path));
    }
    return &*entry;
}

core::Result<std::uint64_t> PakFileSystem::fileSize(const std::filesystem::path& file) const {
    auto entry = entryOf(file);
    if (!entry) {
        return std::unexpected{entry.error()};
    }
    return *entry == nullptr ? manifest_.size() : (*entry)->size;
}

core::Result<std::vector<std::byte>> PakFileSystem::readFile(const std::filesystem::path& file) const {
    auto found = entryOf(file);
    if (!found) {
        return std::unexpected{found.error()};
    }
    if (*found == nullptr) {
        return manifest_;
    }
    const PakEntry* entry = *found;
    const std::string path = normalized(file);
    std::ifstream stream{archive_, std::ios::binary};
    auto bytes = readAt(stream, archive_, entry->offset, entry->storedSize);
    if (!bytes) {
        return bytes;
    }
    if (entry->compression == PakCompression::Zstd) {
        bytes = decompress(*bytes, entry->size);
        if (!bytes) {
            return core::fail(std::format("cannot read '{}': {}; the archive is damaged", path, bytes.error().message));
        }
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
    // A file as it goes into the archive.
    struct Stored {
        core::Result<std::vector<std::byte>> bytes;
        std::uint64_t size = 0;
        PakCompression compression = PakCompression::None;
        core::Sha1Digest sha1{};
    };
    for (std::size_t first = 0; first < files->size(); first += WriteBatch) {
        const std::size_t count = std::min(WriteBatch, files->size() - first);
        std::vector<Stored> batch(count);
        runInParallel(count, [&](std::size_t index) {
            Stored& stored = batch[index];
            stored.bytes = fileSystem.readFile(packRoot / (*files)[first + index].path);
            if (!stored.bytes) {
                return;
            }
            stored.size = stored.bytes->size();
            stored.sha1 = core::sha1(*stored.bytes);
            auto compressed = compress(*stored.bytes);
            if (!compressed) {
                stored.bytes = core::fail(std::format("{}: {}", pathText(packRoot / (*files)[first + index].path),
                                                      compressed.error().message));
            } else if (*compressed) {
                stored.bytes = std::move(**compressed);
                stored.compression = PakCompression::Zstd;
            }
        });
        for (std::size_t index = 0; index < count; ++index) {
            const Stored& stored = batch[index];
            if (!stored.bytes) {
                return std::unexpected{stored.bytes.error()};
            }
            const std::uint64_t offset = alignUp(position, PakAlignment);
            writeBytes(stream, std::span{zeros}.first(static_cast<std::size_t>(offset - position)));
            writeBytes(stream, *stored.bytes);
            position = offset + stored.bytes->size();
            entries.push_back(PakEntry{.path = (*files)[first + index].path,
                                       .offset = offset,
                                       .size = stored.size,
                                       .storedSize = stored.bytes->size(),
                                       .compression = stored.compression,
                                       .sha1 = stored.sha1});
        }
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

core::Result<OpenedPacks> openPacks(const FileSystem& folders, std::span<const FoundPack> packs, PackOrigin origin) {
    OpenedPacks opened;
    for (const FoundPack& pack : packs) {
        if (pack.kind == FoundPack::Kind::Folder) {
            opened.locations.push_back(
                PackLocation{.fileSystem = &folders, .root = pack.path, .fillsGaps = false, .origin = origin});
            continue;
        }
        auto archive = PakFileSystem::open(pack.path);
        if (!archive) {
            return std::unexpected{archive.error()};
        }
        opened.locations.push_back(
            PackLocation{.fileSystem = archive->get(), .root = pack.path, .fillsGaps = false, .origin = origin});
        opened.archives.push_back(std::move(*archive));
    }
    return opened;
}

} // namespace dwe::assets
