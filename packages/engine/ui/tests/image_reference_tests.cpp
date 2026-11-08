#include <dwe/assets/file_system.hpp>
#include <dwe/assets/image.hpp>
#include <dwe/assets/texture.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/ui/image_reference.hpp>

#include <RmlUi/Core/Types.h>
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

#include "render_bridge.hpp"
#include "rml_interfaces.hpp"

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
  REQUIRE(ktxTexture_SetImageFromMemory(ktxTexture(texture), 0, 0, 0, white.data(), white.size()) ==
          KTX_SUCCESS);
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
    memory.addFile("packs/remaster/pack.yaml",
                   "id: remaster\nversion: 0.1.0\nlayer: remaster\npriority: 0\n");
    memory.addFile("packs/base/texture/sheet.png", indexedPng());
    memory.addFile("packs/base/texture/sheet_meta.yaml", IndexedMeta);
    memory.addFile("packs/base/texture/plain.png", indexedPng());
    memory.addFile("packs/base/texture/frame.png", indexedPng());
    memory.addFile("packs/base/ui/skins/modern/frame.png", rgbaPng(Side, Side));
    memory.addFile("packs/remaster/ui/user_frame.png", rgbaPng(Side, Side));
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
  return {image.pixels.at(index * 4), image.pixels.at((index * 4) + 1),
          image.pixels.at((index * 4) + 2), image.pixels.at((index * 4) + 3)};
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

TEST_CASE("an image reference may ask for a grey tone, with or without a palette, in any order") {
  auto both = parseImageReference("/texture/etcdat/sboy?palette=15&tone=grey");
  REQUIRE_MESSAGE(both.has_value(), both.error().message);
  CHECK(both->asset.str() == "texture/etcdat/sboy");
  CHECK(both->palette == std::optional<std::size_t>{15});
  CHECK(both->tone == ImageTone::Grey);
  CHECK(isPixelArt(*both));
  CHECK(imageReferenceText(*both) == "/texture/etcdat/sboy?palette=15&tone=grey");

  auto reversed = parseImageReference("texture/etcdat/sboy?tone=grey&palette=15");
  REQUIRE(reversed.has_value());
  CHECK(imageReferenceText(*reversed) == "/texture/etcdat/sboy?palette=15&tone=grey");

  auto toneOnly = parseImageReference("ui/skins/modern/frame.png?tone=grey");
  REQUIRE(toneOnly.has_value());
  CHECK_FALSE(toneOnly->palette.has_value());
  CHECK(imageReferenceText(*toneOnly) == "/ui/skins/modern/frame?tone=grey");

  CHECK(parseImageReference("texture/time.png")->tone == ImageTone::Natural);
  CHECK_FALSE(parseImageReference("texture/time.png?tone=sepia").has_value());
  CHECK_FALSE(parseImageReference("texture/time.png?palette=1&").has_value());
  CHECK_FALSE(parseImageReference("texture/time.png?size=2").has_value());
  CHECK_FALSE(parseImageReference("texture/time.png?tone").has_value());
}

TEST_CASE(
    "an image reference may ask to be smoothed by the texture upscaler, which is not pixel art") {
  auto smooth = parseImageReference("/ui/x/picture?upscale=smooth&palette=0");
  REQUIRE_MESSAGE(smooth.has_value(), smooth.error().message);
  CHECK(smooth->smooth);
  CHECK(smooth->palette == std::optional<std::size_t>{0});
  CHECK_FALSE(isPixelArt(*smooth));
  CHECK(imageReferenceText(*smooth) == "/ui/x/picture?palette=0&upscale=smooth");

  auto alone = parseImageReference("ui/x/picture?upscale=smooth");
  REQUIRE(alone.has_value());
  CHECK(imageReferenceText(*alone) == "/ui/x/picture?upscale=smooth");

  CHECK_FALSE(parseImageReference("ui/x/picture?palette=0")->smooth);
  CHECK(isPixelArt(*parseImageReference("ui/x/picture?palette=0")));
  CHECK_FALSE(parseImageReference("ui/x/picture?upscale=sharp").has_value());
  CHECK_FALSE(parseImageReference("ui/x/picture?upscale").has_value());
}

TEST_CASE("an image reference may ask for mirrored sides, counts in any order after the rest") {
  auto wide = parseImageReference("/ui/x/picture?mirror_right=26&palette=0&mirror_left=100");
  REQUIRE_MESSAGE(wide.has_value(), wide.error().message);
  CHECK(wide->mirrorLeft == 100);
  CHECK(wide->mirrorRight == 26);
  // Still the disc's pixel art: only smoothing makes it filtered.
  CHECK(isPixelArt(*wide));
  CHECK(imageReferenceText(*wide) == "/ui/x/picture?palette=0&mirror_left=100&mirror_right=26");
  CHECK(imageReferenceText(*parseImageReference("ui/x/picture?mirror_right=4")) ==
        "/ui/x/picture?mirror_right=4");
  CHECK(parseImageReference("ui/x/picture?palette=0")->mirrorLeft == 0);
  CHECK(parseImageReference("ui/x/picture?mirror_left=1024").has_value());
  for (const std::string_view bad :
       {"ui/x/picture?mirror_left=1025", "ui/x/picture?mirror_left=-1",
        "ui/x/picture?mirror_left=", "ui/x/picture?mirror_right=2x", "ui/x/picture?mirror_left"}) {
    CAPTURE(bad);
    CHECK_FALSE(parseImageReference(bad).has_value());
  }
}

namespace {

// A one-row image whose pixels are 1, 2, 3... in every channel.
assets::Image countingRow(std::uint32_t width) {
  assets::Image image{.width = width, .height = 1, .pixels = {}};
  for (std::uint32_t x = 0; x < width; ++x) {
    image.pixels.insert(image.pixels.end(), 4, static_cast<std::byte>(x + 1));
  }
  return image;
}

std::vector<int> rowOf(const assets::Image& image) {
  std::vector<int> values;
  for (std::size_t at = 0; at < image.pixels.size(); at += 4) {
    values.push_back(std::to_integer<int>(image.pixels[at]));
  }
  return values;
}

} // namespace

TEST_CASE("mirrored sides carry the edge columns on, the edge itself repeated, at most the width") {
  const assets::Image wide = withMirroredSides(countingRow(4), 2, 3);
  CHECK(wide.width == 9);
  CHECK(wide.height == 1);
  CHECK(rowOf(wide) == std::vector<int>{2, 1, 1, 2, 3, 4, 4, 3, 2});
  // A side mirrors the whole image at most.
  CHECK(rowOf(withMirroredSides(countingRow(2), 5, 0)) == std::vector<int>{2, 1, 1, 2});
  CHECK(rowOf(withMirroredSides(countingRow(3), 0, 0)) == std::vector<int>{1, 2, 3});
  CHECK(withMirroredSides(assets::Image{}, 2, 2).pixels.empty());
}

TEST_CASE("a smoothed image doubles up to SmoothUpscaleWidth, at most three times") {
  CHECK(smoothDoublings(320) == 3);
  CHECK(smoothDoublings(446) == 3);
  CHECK(smoothDoublings(640) == 2);
  CHECK(smoothDoublings(1280) == 1);
  CHECK(smoothDoublings(SmoothUpscaleWidth) == 0);
  CHECK(smoothDoublings(4096) == 0);
  // A small icon still grows only three times.
  CHECK(smoothDoublings(16) == SmoothUpscaleMaxDoublings);
  CHECK(smoothDoublings(0) == 0);
}

TEST_CASE("a grey image is a light grey silhouette: its shading kept, its alpha untouched") {
  CHECK(greyToneOf(0, 0, 0) == GreyToneDarkest);
  CHECK(greyToneOf(255, 255, 255) == GreyToneLightest);
  // Brighter colours stay brighter: green weighs most, then red, then blue.
  CHECK(greyToneOf(0, 255, 0) > greyToneOf(255, 0, 0));
  CHECK(greyToneOf(255, 0, 0) > greyToneOf(0, 0, 255));

  const Packs packs;
  auto grey = packs.load("texture/sheet.png?palette=1&tone=grey");
  REQUIRE_MESSAGE(grey.has_value(), grey.error().message);
  const auto toned = [](int red, int green, int blue, int alpha) {
    const auto value = greyToneOf(static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green),
                                  static_cast<std::uint8_t>(blue));
    return rgba(value, value, value, alpha);
  };
  CHECK(pixelAt(*grey, 0) == toned(0, 0, 0, 0));
  CHECK(pixelAt(*grey, 1) == toned(255, 0, 0, 255));
  CHECK(pixelAt(*grey, 2) == toned(0, 255, 0, 255));
  CHECK(pixelAt(*grey, 3) == toned(0, 0, 255, 255));
  // Any image takes a tone, not only the disc's indexed ones.
  auto user = packs.load("ui/user_frame.png?tone=grey");
  REQUIRE(user.has_value());
  CHECK(pixelAt(*user, 0) == rgba(GreyToneLightest, GreyToneLightest, GreyToneLightest, 255));

  // The same image in its natural tone is a separate texture, left as it was.
  TextureSources sources{*packs.files};
  RenderBridge bridge{sources, nullptr};
  Rml::Vector2i size;
  const Rml::TextureHandle natural = bridge.LoadTexture(size, "/texture/sheet.png?palette=1");
  const Rml::TextureHandle silhouette =
      bridge.LoadTexture(size, "/texture/sheet.png?palette=1&tone=grey");
  REQUIRE(natural != 0);
  REQUIRE(silhouette != 0);
  CHECK(natural != silhouette);
  CHECK(bridge.drawsSharp(silhouette));
}

TEST_CASE(
    "the disc's pixel art, an indexed image drawn with a palette, is drawn sharp; any other image is filtered") {
  const auto sprite = parseImageReference("/texture/etcdat/feel_ef?palette=1");
  REQUIRE(sprite.has_value());
  CHECK(isPixelArt(*sprite));
  const auto modern = parseImageReference("ui/skins/modern/frame.png");
  REQUIRE(modern.has_value());
  CHECK_FALSE(isPixelArt(*modern));
  // Never guessed from the image: the same indexed sprite named without a palette is filtered.
  const auto unnamed = parseImageReference("/texture/etcdat/feel_ef");
  REQUIRE(unnamed.has_value());
  CHECK_FALSE(isPixelArt(*unnamed));
}

TEST_CASE(
    "only a texture named with a palette draws sharp; font, skin and user images stay linear") {
  const Packs packs;
  TextureSources sources{*packs.files};
  RenderBridge bridge{sources, nullptr};
  Rml::Vector2i size;

  const Rml::TextureHandle sprite = bridge.LoadTexture(size, "/texture/sheet.png?palette=1");
  REQUIRE(sprite != 0);
  CHECK(bridge.drawsSharp(sprite));

  // The same indexed image, with metadata and palettes, named without a palette: the reference
  // decides, not the file.
  const Rml::TextureHandle ownPalette = bridge.LoadTexture(size, "/texture/sheet.png");
  REQUIRE(ownPalette != 0);
  CHECK_FALSE(bridge.drawsSharp(ownPalette));
  for (const std::string_view source :
       {"/ui/skins/modern/frame.png", "/ui/user_frame.png", "/texture/frame"}) {
    CAPTURE(source);
    const Rml::TextureHandle texture = bridge.LoadTexture(size, Rml::String{source});
    REQUIRE(texture != 0);
    CHECK_FALSE(bridge.drawsSharp(texture));
  }

  // Font atlases (FontEngine) and minimaps come through GenerateTexture.
  const std::vector<Rml::byte> atlas(std::size_t{Side} * Side * 4, Rml::byte{255});
  const Rml::TextureHandle font =
      bridge.GenerateTexture(atlas, {static_cast<int>(Side), static_cast<int>(Side)});
  REQUIRE(font != 0);
  CHECK_FALSE(bridge.drawsSharp(font));

  bridge.ReleaseTexture(sprite);
  CHECK_FALSE(bridge.drawsSharp(sprite));
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

TEST_CASE(
    "an image wider than the UI allows is refused from its header, by its size and before it is decoded") {
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
  CHECK(rgbaBytesOverLevels(2, 2, UINT32_MAX) ==
        (std::uint64_t{UINT32_MAX} * BytesPerRgbaPixel) + 12);
}

} // namespace dwe::ui
