#include <dwe/assets/image.hpp>
#include <dwe/assets/texture.hpp>

#include <ktx.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <format>
#include <memory>
#include <mutex>
#include <utility>

namespace dwe::assets {
namespace {

constexpr std::uint32_t BlockSide = 4;
constexpr std::size_t BytesPerBlock = 16;
constexpr std::size_t BytesPerTexel = 4;
// The fixed part of a KTX2 file before its level index: identifier, header fields and the index of its data blocks.
constexpr std::size_t Ktx2HeaderSize = 80;

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

} // namespace

core::Result<TextureData> decodeKtx2(std::span<const std::byte> ktx2, TexelEncoding encoding) {
    // libktx 4.4.2 leaks its memory stream when the header itself is cut short, so that is refused before it.
    if (ktx2.size() < Ktx2HeaderSize) {
        return core::fail(std::format("not a KTX2 texture: {} bytes is shorter than its header", ktx2.size()));
    }
    ktxTexture2* created = nullptr;
    if (const KTX_error_code error =
            ktxTexture2_CreateFromMemory(reinterpret_cast<const ktx_uint8_t*>(ktx2.data()), ktx2.size(),
                                         KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &created);
        error != KTX_SUCCESS) {
        return core::fail(std::format("not a KTX2 texture: {}", ktxErrorString(error)));
    }
    const std::unique_ptr<ktxTexture2, KtxTextureDeleter> texture{created};
    if (texture->numDimensions != 2 || texture->isArray || texture->isCubemap) {
        return core::fail("the KTX2 texture is not a plain 2D texture");
    }
    const std::uint32_t width = texture->baseWidth;
    const std::uint32_t height = texture->baseHeight;
    if (width == 0 || height == 0 || width > MaxImageSide || height > MaxImageSide) {
        return core::fail(std::format("the KTX2 texture is {}x{}, not 1 to {} on a side", width, height, MaxImageSide));
    }
    if (texture->numLevels == 0 || std::cmp_greater(texture->numLevels, std::bit_width(std::max(width, height)))) {
        return core::fail(std::format("a {}x{} texture cannot have {} mip levels", width, height, texture->numLevels));
    }
    if (!ktxTexture2_NeedsTranscoding(texture.get())) {
        return core::fail("the KTX2 texture does not hold Basis Universal data");
    }
    const ktx_transcode_fmt_e target = encoding == TexelEncoding::Bc7 ? KTX_TTF_BC7_RGBA : KTX_TTF_RGBA32;
    if (const KTX_error_code error = transcode(texture.get(), target); error != KTX_SUCCESS) {
        return core::fail(std::format("cannot transcode the KTX2 texture: {}", ktxErrorString(error)));
    }

    TextureData data{.encoding = encoding,
                     .srgb = ktxTexture2_GetOETF(texture.get()) == KHR_DF_TRANSFER_SRGB,
                     .width = width,
                     .height = height,
                     .levels = {}};
    const std::span<const std::byte> texels{reinterpret_cast<const std::byte*>(texture->pData), texture->dataSize};
    for (std::uint32_t level = 0; level < texture->numLevels; ++level) {
        ktx_size_t offset = 0;
        const std::size_t size = levelBytes(encoding, std::max(width >> level, 1U), std::max(height >> level, 1U));
        if (ktxTexture2_GetImageOffset(texture.get(), level, 0, 0, &offset) != KTX_SUCCESS || offset > texels.size() ||
            size > texels.size() - offset) {
            return core::fail(std::format("mip level {} of the KTX2 texture is out of its data", level));
        }
        const auto bytes = texels.subspan(offset, size);
        data.levels.emplace_back(bytes.begin(), bytes.end());
    }
    return data;
}

} // namespace dwe::assets
