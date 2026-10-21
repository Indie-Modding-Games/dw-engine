#include <dwe/assets/file_system.hpp>
#include <dwe/assets/image.hpp>
#include <dwe/assets/texture.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/ui/image_reference.hpp>

#include <doctest/doctest.h>
#include <ktx.h>
#include <lodepng.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dwe::ui {
namespace {

constexpr std::uint32_t Side = 4;

// A 4x4 indexed PNG whose pixels are the palette indices 0 to 3, row by row, with palette 0 grey.
std::vector<std::byte> indexedPng() {
    lodepng::State state;
    for (unsigned entry = 0; entry < 4; ++entry) {
        REQUIRE(lodepng_palette_add(&state.info_png.color, 9, 9, 9, 255) == 0);
        REQUIRE(lodepng_palette_add(&state.info_raw, 9, 9, 9, 255) == 0);
    }
    state.info_png.color.colortype = LCT_PALETTE;
    state.info_png.color.bitdepth = 8;
    state.info_raw.colortype = LCT_PALETTE;
    state.info_raw.bitdepth = 8;
    state.encoder.auto_convert = 0;
    std::vector<unsigned char> indices(std::size_t{Side} * Side);
    for (std::size_t pixel = 0; pixel < indices.size(); ++pixel) {
        indices[pixel] = static_cast<unsigned char>(pixel % 4);
    }
    std::vector<unsigned char> png;
    REQUIRE(lodepng::encode(png, indices, Side, Side, state) == 0);
    const auto* begin = reinterpret_cast<const std::byte*>(png.data());
    return {begin, begin + png.size()};
}

// The image's two palettes, as dwx writes them: the second is transparent, red, green and blue.
constexpr std::string_view IndexedMeta = "mode: indexed8\npalettes:\n- \"2529 2529 2529 2529\"\n"
                                         "- \"0000 001f 03e0 7c00\"\n";

// A 4x4 KTX2 texture with Basis Universal data (UASTC, as `dwx cook` writes it), all white.
std::vector<std::byte> basisKtx2() {
    ktxTextureCreateInfo info{};
    info.vkFormat = 43; // VK_FORMAT_R8G8B8A8_SRGB
    info.baseWidth = Side;
    info.baseHeight = Side;
    info.baseDepth = 1;
    info.numDimensions = 2;
    info.numLevels = 1;
    info.numLayers = 1;
    info.numFaces = 1;
    ktxTexture2* texture = nullptr;
    REQUIRE(ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture) == KTX_SUCCESS);
    std::vector<ktx_uint8_t> white(std::size_t{Side} * Side * 4, 255);
    REQUIRE(ktxTexture_SetImageFromMemory(ktxTexture(texture), 0, 0, 0, white.data(), white.size()) == KTX_SUCCESS);
    ktxBasisParams params{};
    params.structSize = sizeof(params);
    params.uastc = KTX_TRUE;
    params.threadCount = 1;
    params.uastcFlags = KTX_PACK_UASTC_LEVEL_DEFAULT;
    REQUIRE(ktxTexture2_CompressBasisEx(texture, &params) == KTX_SUCCESS);
    ktx_uint8_t* written = nullptr;
    ktx_size_t size = 0;
    const KTX_error_code result = ktxTexture2_WriteToMemory(texture, &written, &size);
    ktxTexture2_Destroy(texture);
    REQUIRE(result == KTX_SUCCESS);
    const auto* begin = reinterpret_cast<const std::byte*>(written);
    std::vector<std::byte> bytes{begin, begin + size};
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory): libktx allocates it with malloc
    std::free(written);
    return bytes;
}

// An opaque RGBA PNG of any size.
std::vector<std::byte> rgbaPng(unsigned width, unsigned height) {
    const std::vector<unsigned char> pixels(std::size_t{width} * height * 4, 255);
    std::vector<unsigned char> png;
    REQUIRE(lodepng::encode(png, pixels, width, height) == 0);
    const auto* begin = reinterpret_cast<const std::byte*>(png.data());
    return {begin, begin + png.size()};
}

// A KTX2 texture with one uncompressed RGBA8 level of any size.
std::vector<std::byte> rgbaKtx2(std::uint32_t width, std::uint32_t height) {
    ktxTextureCreateInfo info{};
    info.vkFormat = 37; // VK_FORMAT_R8G8B8A8_UNORM
    info.baseWidth = width;
    info.baseHeight = height;
    info.baseDepth = 1;
    info.numDimensions = 2;
    info.numLevels = 1;
    info.numLayers = 1;
    info.numFaces = 1;
    ktxTexture2* texture = nullptr;
    REQUIRE(ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture) == KTX_SUCCESS);
    ktx_uint8_t* written = nullptr;
    ktx_size_t size = 0;
    const KTX_error_code result = ktxTexture2_WriteToMemory(texture, &written, &size);
    ktxTexture2_Destroy(texture);
    REQUIRE(result == KTX_SUCCESS);
    const auto* begin = reinterpret_cast<const std::byte*>(written);
    std::vector<std::byte> bytes{begin, begin + size};
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory): libktx allocates it with malloc
    std::free(written);
    return bytes;
}

// A base pack with the extracted images and a remaster that replaces one with a KTX2 texture.
struct Packs {
    assets::MemoryFileSystem memory;
    std::unique_ptr<assets::VirtualFileSystem> files;

    Packs() {
        memory.addFile("packs/base/pack.yaml", "id: base\nversion: 0.1.0\nlayer: base\npriority: 0\n");
        memory.addFile("packs/remaster/pack.yaml", "id: remaster\nversion: 0.1.0\nlayer: remaster\npriority: 0\n");
        memory.addFile("packs/base/texture/sheet.png", indexedPng());
        memory.addFile("packs/base/texture/sheet_meta.yaml", IndexedMeta);
        memory.addFile("packs/base/texture/plain.png", indexedPng());
        memory.addFile("packs/base/texture/frame.png", indexedPng());
        memory.addFile("packs/remaster/texture/frame.ktx2", basisKtx2());
        memory.addFile("packs/base/texture/wide.png", rgbaPng(MaxUiImageSide + 1, 1));
        memory.addFile("packs/remaster/texture/wide_texture.ktx2", rgbaKtx2(MaxUiImageSide + 1, 1));
        const std::vector<std::filesystem::path> roots{"packs/base", "packs/remaster"};
        auto mounted = assets::VirtualFileSystem::mount(memory, roots);
        REQUIRE_MESSAGE(mounted.has_value(), mounted.error().message);
        files = std::make_unique<assets::VirtualFileSystem>(std::move(*mounted));
    }

    [[nodiscard]] core::Result<assets::Image> load(std::string_view path) const {
        auto reference = parseImageReference(path);
        REQUIRE_MESSAGE(reference.has_value(), reference.error().message);
        return loadImage(*files, *reference);
    }

    [[nodiscard]] core::Result<ImageSize> size(std::string_view path) const {
        auto reference = parseImageReference(path);
        REQUIRE_MESSAGE(reference.has_value(), reference.error().message);
        return imageSize(*files, *reference);
    }
};

std::array<std::byte, 4> pixelAt(const assets::Image& image, std::size_t index) {
    return {image.pixels.at(index * 4), image.pixels.at((index * 4) + 1), image.pixels.at((index * 4) + 2),
            image.pixels.at((index * 4) + 3)};
}

constexpr std::array<std::byte, 4> rgba(int red, int green, int blue, int alpha) {
    return {std::byte(red), std::byte(green), std::byte(blue), std::byte(alpha)};
}

} // namespace

TEST_CASE("an image reference names an asset, with or without an extension, and maybe a palette") {
    auto plain = parseImageReference("/texture/stddat/time.png");
    REQUIRE(plain.has_value());
    CHECK(plain->asset.str() == "texture/stddat/time");
    CHECK_FALSE(plain->palette.has_value());

    auto paletted = parseImageReference("texture/stddat/time?palette=12");
    REQUIRE(paletted.has_value());
    CHECK(paletted->asset.str() == "texture/stddat/time");
    CHECK(paletted->palette == std::optional<std::size_t>{12});
    CHECK(imageReferenceText(*paletted) == "/texture/stddat/time?palette=12");

    CHECK_FALSE(parseImageReference("texture/time.png?palette=").has_value());
    CHECK_FALSE(parseImageReference("texture/time.png?palette=x").has_value());
    CHECK_FALSE(parseImageReference("Texture/Time.png").has_value());
}

TEST_CASE("a remaster's KTX2 texture answers the references to the PNG it replaces") {
    const Packs packs;
    for (const std::string_view path : {"texture/frame.png", "texture/frame", "texture/frame.ktx2"}) {
        auto image = packs.load(path);
        REQUIRE_MESSAGE(image.has_value(), image.error().message);
        CHECK(image->width == Side);
        CHECK(image->height == Side);
        CHECK(pixelAt(*image, 0) == rgba(255, 255, 255, 255));
    }
}

TEST_CASE("an indexed image draws with any palette of its metadata") {
    const Packs packs;
    auto own = packs.load("texture/sheet.png");
    REQUIRE(own.has_value());
    CHECK(pixelAt(*own, 1) == rgba(9, 9, 9, 255));

    auto second = packs.load("texture/sheet.png?palette=1");
    REQUIRE_MESSAGE(second.has_value(), second.error().message);
    CHECK(pixelAt(*second, 0) == rgba(0, 0, 0, 0));
    CHECK(pixelAt(*second, 1) == rgba(255, 0, 0, 255));
    CHECK(pixelAt(*second, 2) == rgba(0, 255, 0, 255));
    CHECK(pixelAt(*second, 3) == rgba(0, 0, 255, 255));

    CHECK_FALSE(packs.load("texture/sheet.png?palette=2").has_value());
    // No metadata, or a texture that has no palettes.
    CHECK_FALSE(packs.load("texture/plain.png?palette=1").has_value());
    CHECK_FALSE(packs.load("texture/frame.png?palette=1").has_value());
    CHECK_FALSE(packs.load("texture/missing.png").has_value());
}

TEST_CASE("an image wider than the UI allows is refused from its header, by its size and before it is decoded") {
    const Packs packs;
    for (const std::string_view path : {"texture/wide.png", "texture/wide_texture.png"}) {
        CAPTURE(path);
        const std::string expected =
            std::format("is {}x1, larger than {} on a side", MaxUiImageSide + 1, MaxUiImageSide);
        const auto size = packs.size(path);
        REQUIRE_FALSE(size.has_value());
        CHECK(size.error().message.contains(expected));
        const auto image = packs.load(path);
        REQUIRE_FALSE(image.has_value());
        CHECK(image.error().message.contains(expected));
    }
    const auto frame = packs.size("texture/frame.png");
    REQUIRE_MESSAGE(frame.has_value(), frame.error().message);
    CHECK(frame->width == Side);
}

TEST_CASE("a UI image file larger than MaxUiImageFileBytes is not read") {
    Packs packs;
    const std::vector<std::byte> huge(MaxUiImageFileBytes + 1);
    packs.memory.addFile("packs/base/texture/huge.png", huge);
    const std::vector<std::filesystem::path> roots{"packs/base", "packs/remaster"};
    auto mounted = assets::VirtualFileSystem::mount(packs.memory, roots);
    REQUIRE(mounted.has_value());
    packs.files = std::make_unique<assets::VirtualFileSystem>(std::move(*mounted));
    const auto size = packs.size("texture/huge.png");
    REQUIRE_FALSE(size.has_value());
    CHECK(size.error().message.contains("more than the UI reads"));
}

TEST_CASE("the UI's decode budget is a 4096 square with its full mip chain") {
    CHECK(MaxUiImageBytes == std::uint64_t{89478484}); // 4 bytes times 4^0 + 4^1 + ... + 4^12 texels
    const auto header = [](std::uint32_t width, std::uint32_t height, std::uint32_t levels) {
        return assets::Ktx2Header{.width = width,
                                  .height = height,
                                  .depth = 1,
                                  .dimensions = 2,
                                  .layers = 1,
                                  .faces = 1,
                                  .levels = levels,
                                  .supercompression = assets::Ktx2Supercompression::Zstd,
                                  .needsTranscoding = true,
                                  .levelBytes = {}};
    };
    CHECK(checkUiImageBudget(header(MaxUiImageSide, MaxUiImageSide, 13)).has_value());
    CHECK(checkUiImageBudget(header(MaxUiImageSide, MaxUiImageSide, 1)).has_value());
    const auto fourteen = checkUiImageBudget(header(MaxUiImageSide, MaxUiImageSide, 14));
    REQUIRE_FALSE(fourteen.has_value());
    CHECK(fourteen.error().message.contains("89478488 bytes decoded over its 14 levels"));
    CHECK_FALSE(checkUiImageBudget(header(MaxUiImageSide * 2, MaxUiImageSide, 1)).has_value());
    // A forged level count costs no more than a chain's length to add up.
    CHECK(rgbaBytesOverLevels(2, 2, UINT32_MAX) == (std::uint64_t{UINT32_MAX} * BytesPerRgbaPixel) + 12);
}

} // namespace dwe::ui
