#include "ktx2.hpp"

#include <dwe/core/assert.hpp>

#include <algorithm>
#include <array>
#include <string_view>

namespace dwe::dwx {

namespace {

constexpr std::array<std::uint8_t, 12> Identifier = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
constexpr std::uint32_t HeaderSize = 80;
constexpr std::uint32_t LevelIndexEntrySize = 24;
constexpr std::uint32_t BytesPerTexel = 4;
// Key/value entries and mip levels start at multiples of 4 bytes (lcm of 4 and the texel size).
constexpr std::uint32_t Alignment = 4;

// The basic Data Format Descriptor block (Khronos Data Format Specification 1.3) of an RGBA8 format.
constexpr std::uint32_t DfdVersion = 2;
constexpr std::uint32_t SampleCount = 4;
constexpr std::uint32_t DfdBlockSize = 24 + (16 * SampleCount);
constexpr std::uint32_t DfdTotalSize = 4 + DfdBlockSize;
constexpr std::uint32_t ColorModelRgbsda = 1;
constexpr std::uint32_t ColorPrimariesBt709 = 1;
constexpr std::uint32_t TransferLinear = 1;
constexpr std::uint32_t TransferSrgb = 2;
constexpr std::array<std::uint32_t, SampleCount> Channels = {0, 1, 2, 15}; // red, green, blue, alpha
constexpr std::uint32_t ChannelAlpha = 15;
// Alpha is never sRGB-encoded, so an sRGB format marks its alpha sample linear.
constexpr std::uint32_t SampleLinear = 0x10;
constexpr std::uint32_t SampleBits = 8;
constexpr std::uint32_t SampleUpper = 255;

// Sorted by key, as the format requires. KTXorientation "rd": rows go down from the top, texels go right.
constexpr std::array<std::array<std::string_view, 2>, 2> KeyValues = {{{"KTXorientation", "rd"}, {"KTXwriter", "dwx"}}};

void appendU32(std::vector<std::byte>& bytes, std::uint32_t value) {
    for (std::uint32_t i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<std::byte>((value >> (8U * i)) & 0xFFU));
    }
}

void appendU64(std::vector<std::byte>& bytes, std::uint64_t value) {
    for (std::uint32_t i = 0; i < 8; ++i) {
        bytes.push_back(static_cast<std::byte>((value >> (8U * i)) & 0xFFU));
    }
}

void appendText(std::vector<std::byte>& bytes, std::string_view text) {
    const auto chars = std::as_bytes(std::span{text});
    bytes.insert(bytes.end(), chars.begin(), chars.end());
    bytes.push_back(std::byte{0});
}

void padTo(std::vector<std::byte>& bytes, std::size_t alignment) {
    bytes.resize((bytes.size() + alignment - 1) / alignment * alignment);
}

std::vector<std::byte> dataFormatDescriptor(Ktx2Format format) {
    const bool srgb = format == Ktx2Format::Rgba8Srgb;
    std::vector<std::byte> dfd;
    appendU32(dfd, DfdTotalSize);
    appendU32(dfd, 0); // vendor Khronos, descriptor type basic
    appendU32(dfd, DfdVersion | (DfdBlockSize << 16U));
    appendU32(dfd, ColorModelRgbsda | (ColorPrimariesBt709 << 8U) | ((srgb ? TransferSrgb : TransferLinear) << 16U));
    appendU32(dfd, 0);             // texel block 1x1x1x1, stored as size - 1
    appendU32(dfd, BytesPerTexel); // bytes in plane 0; planes 1-3 empty
    appendU32(dfd, 0);             // planes 4-7 empty
    for (std::uint32_t i = 0; i < SampleCount; ++i) {
        const std::uint32_t channel = Channels[i] | (srgb && Channels[i] == ChannelAlpha ? SampleLinear : 0U);
        appendU32(dfd, (i * SampleBits) | ((SampleBits - 1) << 16U) | (channel << 24U));
        appendU32(dfd, 0); // sample position
        appendU32(dfd, 0); // lower
        appendU32(dfd, SampleUpper);
    }
    return dfd;
}

std::vector<std::byte> keyValueData() {
    std::vector<std::byte> kvd;
    for (const auto& [key, value] : KeyValues) {
        appendU32(kvd, static_cast<std::uint32_t>(key.size() + 1 + value.size() + 1));
        appendText(kvd, key);
        appendText(kvd, value);
        padTo(kvd, Alignment);
    }
    return kvd;
}

} // namespace

std::vector<std::byte> writeKtx2(Ktx2Format format, std::span<const assets::Image> levels) {
    DWE_ASSERT(!levels.empty());
    for (std::size_t i = 1; i < levels.size(); ++i) {
        DWE_ASSERT(levels[i].width == std::max(levels[i - 1].width / 2, 1U));
        DWE_ASSERT(levels[i].height == std::max(levels[i - 1].height / 2, 1U));
    }
    const auto levelCount = static_cast<std::uint32_t>(levels.size());
    const auto dfd = dataFormatDescriptor(format);
    const auto kvd = keyValueData();
    const std::uint32_t dfdOffset = HeaderSize + (LevelIndexEntrySize * levelCount);
    const auto kvdOffset = static_cast<std::uint32_t>(dfdOffset + dfd.size());

    std::vector<std::byte> bytes(Identifier.size());
    std::ranges::transform(Identifier, bytes.begin(), [](std::uint8_t value) { return std::byte{value}; });
    appendU32(bytes, static_cast<std::uint32_t>(format));
    appendU32(bytes, 1); // typeSize: bytes per component
    appendU32(bytes, levels.front().width);
    appendU32(bytes, levels.front().height);
    appendU32(bytes, 0); // pixelDepth: 2D
    appendU32(bytes, 0); // layerCount: not an array
    appendU32(bytes, 1); // faceCount: not a cube map
    appendU32(bytes, levelCount);
    appendU32(bytes, 0); // supercompressionScheme: none
    appendU32(bytes, dfdOffset);
    appendU32(bytes, static_cast<std::uint32_t>(dfd.size()));
    appendU32(bytes, kvdOffset);
    appendU32(bytes, static_cast<std::uint32_t>(kvd.size()));
    appendU64(bytes, 0); // no supercompression global data
    appendU64(bytes, 0);

    // The format stores the smallest level first, so a loader can stream the large ones last.
    std::vector<std::uint64_t> offsets(levels.size());
    std::uint64_t offset = (kvdOffset + kvd.size() + Alignment - 1) / Alignment * Alignment;
    for (std::size_t i = levels.size(); i-- > 0;) {
        offsets[i] = offset;
        offset += levels[i].pixels.size();
    }
    for (std::size_t i = 0; i < levels.size(); ++i) {
        DWE_ASSERT(levels[i].pixels.size() == std::size_t{levels[i].width} * levels[i].height * BytesPerTexel);
        appendU64(bytes, offsets[i]);
        appendU64(bytes, levels[i].pixels.size());
        appendU64(bytes, levels[i].pixels.size()); // uncompressed size: the same without supercompression
    }
    bytes.insert(bytes.end(), dfd.begin(), dfd.end());
    bytes.insert(bytes.end(), kvd.begin(), kvd.end());
    padTo(bytes, Alignment);
    for (std::size_t i = levels.size(); i-- > 0;) {
        bytes.insert(bytes.end(), levels[i].pixels.begin(), levels[i].pixels.end());
    }
    return bytes;
}

} // namespace dwe::dwx
