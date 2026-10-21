#include <dwe/assets/image.hpp>
#include <dwe/assets/texture.hpp>
#include <dwe/core/message.hpp>

#include <ktx.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include "messages.hpp"

namespace dwe::assets {
namespace {

constexpr std::uint32_t BlockSide = 4;
constexpr std::size_t BytesPerBlock = 16;
constexpr std::size_t BytesPerTexel = 4;
// The fixed part of a KTX2 file before its level index: identifier, header fields and the index of its data blocks.
constexpr std::size_t Ktx2HeaderSize = 80;
// Each level's entry in the level index: byteOffset, byteLength and uncompressedByteLength, 64 bits each.
constexpr std::size_t Ktx2LevelEntrySize = 24;
constexpr std::size_t Ktx2LevelLengthOffset = 8;
constexpr std::size_t Ktx2LevelInflatedOffset = 16;
constexpr unsigned BitsPerByte = 8;
// Where the header gives the offset and length of the data format descriptor, the key/value data (32 bits each) and
// the supercompression global data (64 bits each).
constexpr std::size_t Ktx2DfdAt = 48;
constexpr std::size_t Ktx2KvdAt = 56;
constexpr std::size_t Ktx2SgdAt = 64;
constexpr std::size_t U32Size = 4;
constexpr std::size_t U64Size = 8;

struct KtxTextureDeleter {
    void operator()(ktxTexture2* texture) const { ktxTexture2_Destroy(texture); }
};

std::size_t levelBytes(TexelEncoding encoding, std::uint32_t width, std::uint32_t height) {
    if (encoding == TexelEncoding::Bc7) {
        return std::size_t{(width + BlockSide - 1) / BlockSide} * ((height + BlockSide - 1) / BlockSide) *
               BytesPerBlock;
    }
    return std::size_t{width} * height * BytesPerTexel;
}

// libktx builds the transcoder's tables on the first transcode, without a lock, so that one runs alone.
KTX_error_code transcode(ktxTexture2* texture, ktx_transcode_fmt_e format) {
    static std::atomic<bool> initialized{false};
    static std::mutex first;
    if (initialized.load(std::memory_order_acquire)) {
        return ktxTexture2_TranscodeBasis(texture, format, 0);
    }
    const std::scoped_lock lock{first};
    const KTX_error_code result = ktxTexture2_TranscodeBasis(texture, format, 0);
    if (result == KTX_SUCCESS) {
        initialized.store(true, std::memory_order_release);
    }
    return result;
}

// A little-endian number of 1 to 8 bytes.
std::uint64_t readLittleEndian(std::span<const std::byte> bytes, std::size_t size) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < size; ++i) {
        value |= std::uint64_t{std::to_integer<std::uint8_t>(bytes[i])} << (i * BitsPerByte);
    }
    return value;
}

std::uint64_t readU64(std::span<const std::byte> bytes) {
    return readLittleEndian(bytes, U64Size);
}

// Refuses a header whose data format descriptor, key/value data or supercompression global data runs past the file,
// before libktx reads them.
core::Result<void> checkKtx2Regions(std::span<const std::byte> ktx2) {
    struct Region {
        std::string_view name;
        std::size_t at;
        std::size_t fieldSize;
    };
    constexpr std::array<Region, 3> Regions{
        Region{.name = "data format descriptor", .at = Ktx2DfdAt, .fieldSize = U32Size},
        Region{.name = "key/value data", .at = Ktx2KvdAt, .fieldSize = U32Size},
        Region{.name = "supercompression global data", .at = Ktx2SgdAt, .fieldSize = U64Size}};
    for (const Region& region : Regions) {
        const std::uint64_t offset = readLittleEndian(ktx2.subspan(region.at), region.fieldSize);
        const std::uint64_t length = readLittleEndian(ktx2.subspan(region.at + region.fieldSize), region.fieldSize);
        if (offset > ktx2.size() || length > ktx2.size() - offset) {
            return core::fail(
                core::formatMessage(messages::Ktx2RegionOutsideFile, region.name, length, offset, ktx2.size()));
        }
    }
    return {};
}

Ktx2Supercompression supercompressionOf(ktxSupercmpScheme scheme) {
    switch (scheme) {
    case KTX_SS_NONE:
        return Ktx2Supercompression::None;
    case KTX_SS_BASIS_LZ:
        return Ktx2Supercompression::BasisLz;
    case KTX_SS_ZSTD:
        return Ktx2Supercompression::Zstd;
    default:
        return Ktx2Supercompression::Other;
    }
}

// Opens a KTX2 texture without loading its texel data, and reads its header and level index.
core::Result<std::pair<std::unique_ptr<ktxTexture2, KtxTextureDeleter>, Ktx2Header>>
openKtx2(std::span<const std::byte> ktx2) {
    // libktx 4.4.2 leaks its memory stream when the header itself is cut short, so that is refused before it.
    if (ktx2.size() < Ktx2HeaderSize) {
        return core::fail(core::formatMessage(messages::Ktx2TooShort, ktx2.size()));
    }
    if (auto regions = checkKtx2Regions(ktx2); !regions) {
        return std::unexpected{regions.error()};
    }
    ktxTexture2* created = nullptr;
    if (const KTX_error_code error = ktxTexture2_CreateFromMemory(reinterpret_cast<const ktx_uint8_t*>(ktx2.data()),
                                                                  ktx2.size(), KTX_TEXTURE_CREATE_NO_FLAGS, &created);
        error != KTX_SUCCESS) {
        return core::fail(core::formatMessage(messages::NotKtx2, ktxErrorString(error)));
    }
    std::unique_ptr<ktxTexture2, KtxTextureDeleter> texture{created};
    const std::uint32_t levels = texture->numLevels;
    if (std::uint64_t{levels} * Ktx2LevelEntrySize > ktx2.size() - Ktx2HeaderSize) {
        return core::fail(core::formatMessage(messages::Ktx2LevelIndexCut, levels, ktx2.size()));
    }
    Ktx2Header header{.width = texture->baseWidth,
                      .height = texture->baseHeight,
                      .depth = texture->baseDepth,
                      .dimensions = texture->numDimensions,
                      .layers = texture->numLayers,
                      .faces = texture->numFaces,
                      .levels = levels,
                      .supercompression = supercompressionOf(texture->supercompressionScheme),
                      .needsTranscoding = ktxTexture2_NeedsTranscoding(texture.get()),
                      .levelBytes = {}};
    header.levelBytes.reserve(levels);
    for (std::uint32_t level = 0; level < levels; ++level) {
        const auto entry = ktx2.subspan(Ktx2HeaderSize + (std::size_t{level} * Ktx2LevelEntrySize), Ktx2LevelEntrySize);
        const std::uint64_t offset = readU64(entry);
        const std::uint64_t stored = readU64(entry.subspan(Ktx2LevelLengthOffset));
        if (offset > ktx2.size() || stored > ktx2.size() - offset) {
            return core::fail(core::formatMessage(messages::Ktx2LevelOutsideFile, level, ktx2.size()));
        }
        header.levelBytes.push_back(header.supercompression == Ktx2Supercompression::Zstd
                                        ? readU64(entry.subspan(Ktx2LevelInflatedOffset))
                                        : stored);
    }
    return std::pair{std::move(texture), std::move(header)};
}

} // namespace

core::Result<Ktx2Header> inspectKtx2(std::span<const std::byte> ktx2) {
    auto opened = openKtx2(ktx2);
    if (!opened) {
        return std::unexpected{opened.error()};
    }
    return std::move(opened->second);
}

core::Result<void> checkPlain2dKtx2(const Ktx2Header& header, std::uint32_t maxSide) {
    if (header.dimensions != 2 || header.depth > 1 || header.layers != 1 || header.faces != 1) {
        return core::fail(core::formatMessage(messages::Ktx2NotPlain2d, header.dimensions, header.depth, header.layers,
                                              header.faces));
    }
    const std::uint32_t width = header.width;
    const std::uint32_t height = header.height;
    if (width == 0 || height == 0 || width > maxSide || height > maxSide) {
        return core::fail(core::formatMessage(messages::Ktx2BadSize, width, height, maxSide));
    }
    if (header.levels == 0 || std::cmp_greater(header.levels, std::bit_width(std::max(width, height))) ||
        header.levelBytes.size() != header.levels) {
        return core::fail(core::formatMessage(messages::Ktx2BadLevels, width, height, header.levels));
    }
    if (header.supercompression == Ktx2Supercompression::Other) {
        return core::fail(std::string{messages::Ktx2BadSupercompression});
    }
    // No texel format takes more than either 4 bytes a texel or 16 bytes a block of 4x4. BasisLZ levels are loaded as
    // stored, so the file's own size bounds them.
    for (std::uint32_t level = 0; header.supercompression != Ktx2Supercompression::BasisLz && level < header.levels;
         ++level) {
        const std::uint32_t levelWidth = std::max(width >> level, 1U);
        const std::uint32_t levelHeight = std::max(height >> level, 1U);
        const std::uint64_t most = std::max<std::uint64_t>(levelBytes(TexelEncoding::Bc7, levelWidth, levelHeight),
                                                           levelBytes(TexelEncoding::Rgba8, levelWidth, levelHeight));
        if (header.levelBytes[level] > most) {
            return core::fail(core::formatMessage(messages::Ktx2LevelTooLarge, level, header.levelBytes[level], most,
                                                  levelWidth, levelHeight));
        }
    }
    if (!header.needsTranscoding) {
        return core::fail(std::string{messages::Ktx2NotBasis});
    }
    return {};
}

core::Result<TextureData> decodeKtx2(std::span<const std::byte> ktx2, TexelEncoding encoding) {
    auto opened = openKtx2(ktx2);
    if (!opened) {
        return std::unexpected{opened.error()};
    }
    auto& [texture, header] = *opened;
    if (auto plain = checkPlain2dKtx2(header, MaxImageSide); !plain) {
        return std::unexpected{plain.error()};
    }
    // Only now, with the levels' sizes checked, are the texels read and inflated.
    if (const KTX_error_code error = ktxTexture2_LoadImageData(texture.get(), nullptr, 0); error != KTX_SUCCESS) {
        return core::fail(core::formatMessage(messages::Ktx2LoadFailed, ktxErrorString(error)));
    }
    const ktx_transcode_fmt_e target = encoding == TexelEncoding::Bc7 ? KTX_TTF_BC7_RGBA : KTX_TTF_RGBA32;
    if (const KTX_error_code error = transcode(texture.get(), target); error != KTX_SUCCESS) {
        return core::fail(core::formatMessage(messages::Ktx2TranscodeFailed, ktxErrorString(error)));
    }

    const std::uint32_t width = header.width;
    const std::uint32_t height = header.height;
    TextureData data{.encoding = encoding,
                     .srgb = ktxTexture2_GetOETF(texture.get()) == KHR_DF_TRANSFER_SRGB,
                     .width = width,
                     .height = height,
                     .levels = {}};
    const std::span<const std::byte> texels{reinterpret_cast<const std::byte*>(texture->pData), texture->dataSize};
    for (std::uint32_t level = 0; level < header.levels; ++level) {
        ktx_size_t offset = 0;
        const std::size_t size = levelBytes(encoding, std::max(width >> level, 1U), std::max(height >> level, 1U));
        if (ktxTexture2_GetImageOffset(texture.get(), level, 0, 0, &offset) != KTX_SUCCESS || offset > texels.size() ||
            size > texels.size() - offset) {
            return core::fail(core::formatMessage(messages::Ktx2LevelOutOfData, level));
        }
        const auto bytes = texels.subspan(offset, size);
        data.levels.emplace_back(bytes.begin(), bytes.end());
    }
    return data;
}

} // namespace dwe::assets
