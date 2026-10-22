#include "texture_cooker.hpp"

#include <dwe/assets/png_only_images.hpp>

#include <ktx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <memory>
#include <mutex>
#include <string_view>

namespace dwe::dwx {

namespace {

constexpr std::size_t Channels = 4;
constexpr std::size_t Alpha = 3;
constexpr float MaxByte = 255.0F;

// The Vulkan formats of the RGBA8 levels handed to the encoder (VK_FORMAT_R8G8B8A8_UNORM and
// _SRGB).
constexpr ktx_uint32_t VkFormatRgba8Unorm = 37;
constexpr ktx_uint32_t VkFormatRgba8Srgb = 43;
// Fixed, so the same image always cooks to the same bytes; libktx appends its own version to the
// writer.
constexpr std::string_view Writer = "dwx cook";
constexpr std::string_view Orientation = "rd"; // rows from the top, columns from the left
constexpr ktx_uint32_t ZstdLevel = 18;

// An image in linear light with colour premultiplied by alpha: what box filtering averages.
struct LinearImage {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<float> texels; // width * height * 4
};

// A source texel that a target texel covers, and the share of the target it makes up.
struct Tap {
  std::uint32_t source = 0;
  float weight = 0.0F;
};

// The taps of each target texel along one axis. In units of 1 / (source * target) of the axis,
// target texel i spans [i * source, (i + 1) * source) and source texel j spans [j * target, (j + 1)
// * target), so the overlaps are exact.
std::vector<std::vector<Tap>> axisTaps(std::uint32_t source, std::uint32_t target) {
  std::vector<std::vector<Tap>> taps(target);
  for (std::uint32_t i = 0; i < target; ++i) {
    const std::uint64_t begin = std::uint64_t{i} * source;
    const std::uint64_t end = begin + source;
    for (std::uint64_t j = begin / target; j * target < end; ++j) {
      const std::uint64_t overlap = std::min(end, (j + 1) * target) - std::max(begin, j * target);
      taps[i].push_back(Tap{.source = static_cast<std::uint32_t>(j),
                            .weight = static_cast<float>(overlap) / static_cast<float>(source)});
    }
  }
  return taps;
}

float srgbToLinear(float value) {
  return value <= 0.04045F ? value / 12.92F : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

float linearToSrgb(float value) {
  return value <= 0.0031308F ? value * 12.92F : (1.055F * std::pow(value, 1.0F / 2.4F)) - 0.055F;
}

std::byte toByte(float value) {
  return static_cast<std::byte>(std::lround(std::clamp(value, 0.0F, 1.0F) * MaxByte));
}

LinearImage toLinear(const assets::Image& image, bool srgb) {
  std::array<float, 256> decode{};
  for (std::size_t i = 0; i < decode.size(); ++i) {
    const float value = static_cast<float>(i) / MaxByte;
    decode[i] = srgb ? srgbToLinear(value) : value;
  }
  LinearImage linear{.width = image.width,
                     .height = image.height,
                     .texels = std::vector<float>(image.pixels.size())};
  for (std::size_t texel = 0; texel < image.pixels.size(); texel += Channels) {
    const float alpha =
        static_cast<float>(std::to_integer<std::uint8_t>(image.pixels[texel + Alpha])) / MaxByte;
    for (std::size_t channel = 0; channel < Alpha; ++channel) {
      linear.texels[texel + channel] =
          decode[std::to_integer<std::size_t>(image.pixels[texel + channel])] * alpha;
    }
    linear.texels[texel + Alpha] = alpha;
  }
  return linear;
}

assets::Image toImage(const LinearImage& linear, bool srgb) {
  assets::Image image{.width = linear.width,
                      .height = linear.height,
                      .pixels = std::vector<std::byte>(linear.texels.size())};
  for (std::size_t texel = 0; texel < linear.texels.size(); texel += Channels) {
    const float alpha = linear.texels[texel + Alpha];
    for (std::size_t channel = 0; channel < Alpha; ++channel) {
      const float value = alpha > 0.0F ? linear.texels[texel + channel] / alpha : 0.0F;
      image.pixels[texel + channel] = toByte(srgb ? linearToSrgb(value) : value);
    }
    image.pixels[texel + Alpha] = toByte(alpha);
  }
  return image;
}

// Halves an image with a separable box filter: first along x, then along y.
LinearImage halve(const LinearImage& source) {
  const std::uint32_t width = std::max(source.width / 2, 1U);
  const std::uint32_t height = std::max(source.height / 2, 1U);
  const auto columns = axisTaps(source.width, width);
  const auto rows = axisTaps(source.height, height);
  std::vector<float> narrow(std::size_t{width} * source.height * Channels);
  for (std::size_t y = 0; y < source.height; ++y) {
    for (std::size_t x = 0; x < width; ++x) {
      for (const Tap& tap : columns[x]) {
        for (std::size_t channel = 0; channel < Channels; ++channel) {
          narrow[(((y * width) + x) * Channels) + channel] +=
              source.texels[(((y * source.width) + tap.source) * Channels) + channel] * tap.weight;
        }
      }
    }
  }
  LinearImage halved{.width = width,
                     .height = height,
                     .texels = std::vector<float>(std::size_t{width} * height * Channels)};
  for (std::size_t y = 0; y < height; ++y) {
    for (const Tap& tap : rows[y]) {
      for (std::size_t x = 0; x < width; ++x) {
        for (std::size_t channel = 0; channel < Channels; ++channel) {
          halved.texels[(((y * width) + x) * Channels) + channel] +=
              narrow[(((std::size_t{tap.source} * width) + x) * Channels) + channel] * tap.weight;
        }
      }
    }
  }
  return halved;
}

struct KtxTextureDeleter {
  void operator()(ktxTexture2* texture) const { ktxTexture2_Destroy(texture); }
};

struct KtxBytesDeleter {
  // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory): libktx allocates it with malloc.
  void operator()(ktx_uint8_t* bytes) const { std::free(bytes); }
};

core::Result<void> check(KTX_error_code result, std::string_view what) {
  if (result != KTX_SUCCESS) {
    return core::fail(std::format("libktx cannot {}: {}", what, ktxErrorString(result)));
  }
  return {};
}

core::Result<void> addMetadata(ktxTexture2* texture, std::string_view key, std::string_view value) {
  // The values are NUL-terminated strings, as the KTX2 specification asks for these keys.
  const std::string text{value};
  return check(ktxHashList_AddKVPair(&texture->kvDataHead, std::string{key}.c_str(),
                                     static_cast<ktx_uint32_t>(text.size() + 1), text.c_str()),
               "add the metadata");
}

// Encodes the levels to UASTC, supercompresses them with zstd and writes the KTX2 file.
// Single-threaded, so the bytes depend only on the levels, the colour space and the pinned libktx
// and zstd.
core::Result<std::vector<std::byte>> encodeUastc(std::span<const assets::Image> levels, bool srgb) {
  ktxTextureCreateInfo info{};
  info.vkFormat = srgb ? VkFormatRgba8Srgb : VkFormatRgba8Unorm;
  info.baseWidth = levels.front().width;
  info.baseHeight = levels.front().height;
  info.baseDepth = 1;
  info.numDimensions = 2;
  info.numLevels = static_cast<ktx_uint32_t>(levels.size());
  info.numLayers = 1;
  info.numFaces = 1;
  info.isArray = KTX_FALSE;
  info.generateMipmaps = KTX_FALSE;
  ktxTexture2* created = nullptr;
  if (auto made = check(ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &created),
                        "create a texture");
      !made) {
    return std::unexpected{made.error()};
  }
  const std::unique_ptr<ktxTexture2, KtxTextureDeleter> texture{created};
  for (std::size_t level = 0; level < levels.size(); ++level) {
    const auto& pixels = levels[level].pixels;
    if (auto set = check(ktxTexture_SetImageFromMemory(
                             ktxTexture(texture.get()), static_cast<ktx_uint32_t>(level), 0, 0,
                             reinterpret_cast<const ktx_uint8_t*>(pixels.data()), pixels.size()),
                         "set a mip level");
        !set) {
      return std::unexpected{set.error()};
    }
  }
  for (const auto& [key, value] : {std::pair{std::string_view{KTX_ORIENTATION_KEY}, Orientation},
                                   std::pair{std::string_view{KTX_WRITER_KEY}, Writer}}) {
    if (auto added = addMetadata(texture.get(), key, value); !added) {
      return std::unexpected{added.error()};
    }
  }

  ktxBasisParams params{};
  params.structSize = sizeof(params);
  params.uastc = KTX_TRUE;
  params.threadCount = 1; // dwx cook runs several textures at once instead
  params.uastcFlags = KTX_PACK_UASTC_LEVEL_DEFAULT;
  params.uastcRDO = KTX_FALSE;
  if (auto encoded = check(ktxTexture2_CompressBasisEx(texture.get(), &params), "encode UASTC");
      !encoded) {
    return std::unexpected{encoded.error()};
  }
  if (auto deflated =
          check(ktxTexture2_DeflateZstd(texture.get(), ZstdLevel), "supercompress with zstd");
      !deflated) {
    return std::unexpected{deflated.error()};
  }
  // Writing appends libktx's version to the writer: it unlinks the entry added above and adds
  // another, but does not free the old one (libktx 4.4.2), so it is freed here once it is no longer
  // in the list.
  ktxHashListEntry* writer = nullptr;
  ktxHashList_FindEntry(&texture->kvDataHead, KTX_WRITER_KEY, &writer);
  ktx_uint8_t* written = nullptr;
  ktx_size_t size = 0;
  const KTX_error_code result = ktxTexture2_WriteToMemory(texture.get(), &written, &size);
  if (ktxHashListEntry* current = nullptr;
      writer != nullptr &&
      (ktxHashList_FindEntry(&texture->kvDataHead, KTX_WRITER_KEY, &current) != KTX_SUCCESS ||
       current != writer)) {
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory): libktx allocates it with malloc.
    std::free(writer);
  }
  if (auto wrote = check(result, "write the KTX2 file"); !wrote) {
    return std::unexpected{wrote.error()};
  }
  const std::unique_ptr<ktx_uint8_t, KtxBytesDeleter> owned{written};
  const auto bytes = std::as_bytes(std::span{owned.get(), size});
  return std::vector<std::byte>{bytes.begin(), bytes.end()};
}

// libktx sets up the Basis Universal encoder on its first use without a lock, so that first use
// runs alone, on a 1x1 texture.
void initializeEncoder() {
  static std::once_flag once;
  std::call_once(once, [] {
    const assets::Image texel{.width = 1, .height = 1, .pixels = std::vector<std::byte>(Channels)};
    [[maybe_unused]] const auto warmUp = encodeUastc(std::span{&texel, 1}, true);
  });
}

// An image's palette is chosen while the game runs when the same pixels are drawn with more than
// one CLUT row: the sidecar lists several palettes (creature textures, whose primitives each pick a
// row; effects; multi-palette TIMs), or the image is a map TIM with a CLUT, whose sprites each pick
// a row (data.yaml: clut; -1 is row 480, which the time of day rewrites).
bool paletteChosenAtRuntime(const core::YamlNode& sidecar) {
  if (const core::YamlNode* palettes = sidecar.find("palettes");
      palettes != nullptr && palettes->isSequence() && palettes->items().size() > 1) {
    return true;
  }
  const core::YamlNode* source = sidecar.find("source");
  return sidecar.find("clut_rect") != nullptr && source != nullptr && source->isScalar() &&
         (source->text().ends_with(".MAP") || source->text().ends_with(".map"));
}

} // namespace

std::vector<assets::Image> buildMipChain(const assets::Image& image, bool srgb) {
  std::vector<assets::Image> levels{image};
  LinearImage linear = toLinear(image, srgb);
  while (linear.width > 1 || linear.height > 1) {
    linear = halve(linear);
    levels.push_back(toImage(linear, srgb));
  }
  return levels;
}

core::Result<CookOptions> planTexture(const assets::AssetId& id, std::span<const std::byte> png,
                                      const core::YamlNode* sidecar) {
  const auto header = assets::inspectPng(png);
  if (!header) {
    return std::unexpected{header.error()};
  }
  CookOptions options{.passThrough = false, .colorSpace = ColorSpace::Srgb};
  if (sidecar != nullptr && !sidecar->isMapping()) {
    return core::fail("its sidecar is not a mapping");
  }
  if (assets::pngReadingOf(id, sidecar)) {
    options.passThrough = true;
    return options;
  }
  if (sidecar == nullptr) {
    return options;
  }
  // Only an indexed PNG keeps a palette to choose; any other is cooked as the colours it shows.
  options.passThrough = header->indexed && paletteChosenAtRuntime(*sidecar);
  if (const core::YamlNode* space = sidecar->find("color_space")) {
    if (space->isScalar() && space->text() == "linear") {
      options.colorSpace = ColorSpace::Linear;
    } else if (!space->isScalar() || space->text() != "srgb") {
      return core::fail("its sidecar's color_space is not srgb or linear");
    }
  }
  return options;
}

core::Result<std::vector<std::byte>> cookTexture(std::span<const std::byte> png,
                                                 const CookOptions& options) {
  const auto image = assets::decodePng(png);
  if (!image) {
    return std::unexpected{image.error()};
  }
  initializeEncoder();
  const bool srgb = options.colorSpace == ColorSpace::Srgb;
  return encodeUastc(buildMipChain(*image, srgb), srgb);
}

} // namespace dwe::dwx
