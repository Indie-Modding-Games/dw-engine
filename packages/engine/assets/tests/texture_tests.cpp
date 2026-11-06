#include <dwe/assets/image.hpp>
#include <dwe/assets/texture.hpp>

#include <doctest/doctest.h>
#include <ktx.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

using dwe::assets::checkPlain2dKtx2;
using dwe::assets::decodeKtx2;
using dwe::assets::inspectKtx2;
using dwe::assets::Ktx2Supercompression;
using dwe::assets::TexelEncoding;

namespace {

constexpr ktx_uint32_t RgbaUnorm = 37; // VK_FORMAT_R8G8B8A8_UNORM

// How a test texture is laid out and stored.
struct Ktx2Shape {
  ktx_uint32_t width = 4;
  ktx_uint32_t height = 4;
  ktx_uint32_t layers = 1;
  ktx_uint32_t faces = 1;
  bool basis = false; // UASTC, as dwx cook writes it; otherwise uncompressed RGBA8
  bool zstd = false;  // supercompressed with Zstd
};

std::vector<std::byte> writtenBytes(ktxTexture2* texture) {
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

// A KTX2 texture with one level, white, written by libktx.
std::vector<std::byte> ktx2Of(const Ktx2Shape& shape) {
  ktxTextureCreateInfo info{};
  info.vkFormat = RgbaUnorm;
  info.baseWidth = shape.width;
  info.baseHeight = shape.height;
  info.baseDepth = 1;
  info.numDimensions = 2;
  info.numLevels = 1;
  info.numLayers = shape.layers;
  info.numFaces = shape.faces;
  info.isArray = shape.layers > 1 ? KTX_TRUE : KTX_FALSE;
  ktxTexture2* texture = nullptr;
  REQUIRE(ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture) == KTX_SUCCESS);
  std::fill_n(texture->pData, texture->dataSize, ktx_uint8_t{255});
  if (shape.basis) {
    ktxBasisParams params{};
    params.structSize = sizeof(params);
    params.uastc = KTX_TRUE;
    params.threadCount = 1;
    params.uastcFlags = KTX_PACK_UASTC_LEVEL_FASTEST;
    REQUIRE(ktxTexture2_CompressBasisEx(texture, &params) == KTX_SUCCESS);
  }
  if (shape.zstd) {
    REQUIRE(ktxTexture2_DeflateZstd(texture, 1) == KTX_SUCCESS);
  }
  return writtenBytes(texture);
}

// A 4x4 KTX2 texture with one uncompressed RGBA8 level.
std::vector<std::byte> uncompressedKtx2() {
  return ktx2Of({});
}

// Writes a 64-bit little-endian number into a file's bytes.
void writeU64(std::vector<std::byte>& bytes, std::size_t at, std::uint64_t value) {
  for (std::size_t i = 0; i < sizeof(value); ++i) {
    bytes[at + i] = static_cast<std::byte>((value >> (i * 8)) & 0xFFU);
  }
}

// Where the header gives the length of the supercompression global data.
constexpr std::size_t SgdLengthAt = 72;

// The header of a 4x4 UASTC texture with one level, as a test edits it.
dwe::assets::Ktx2Header plainHeader() {
  return {.width = 4,
          .height = 4,
          .depth = 1,
          .dimensions = 2,
          .layers = 1,
          .faces = 1,
          .levels = 1,
          .supercompression = Ktx2Supercompression::Zstd,
          .needsTranscoding = true,
          .levelBytes = {16}};
}

// Where the first level's uncompressedByteLength sits: after the 80-byte header, byteOffset and
// byteLength.
constexpr std::size_t FirstLevelInflatedAt = 80 + 16;

std::string errorOf(const dwe::core::Result<dwe::assets::TextureData>& result) {
  return result ? std::string{} : result.error().message;
}

} // namespace

TEST_CASE("the KTX2 decoder refuses what is not a KTX2 texture") {
  CHECK(errorOf(decodeKtx2({}, TexelEncoding::Rgba8)).starts_with("not a KTX2 texture"));
  const std::vector<std::byte> cut(13, std::byte{0x5A});
  CHECK(errorOf(decodeKtx2(cut, TexelEncoding::Rgba8)) ==
        "not a KTX2 texture: 13 bytes is shorter than its header");
  const std::vector<std::byte> garbage(256, std::byte{0x5A});
  CHECK(errorOf(decodeKtx2(garbage, TexelEncoding::Bc7)).starts_with("not a KTX2 texture"));
}

TEST_CASE("the KTX2 decoder takes only Basis Universal data") {
  const auto bytes = uncompressedKtx2();
  CHECK(errorOf(decodeKtx2(bytes, TexelEncoding::Rgba8)) ==
        "the KTX2 texture does not hold Basis Universal data");
}

TEST_CASE("a truncated KTX2 texture is refused") {
  auto bytes = uncompressedKtx2();
  bytes.resize(bytes.size() - 1);
  CHECK(errorOf(decodeKtx2(bytes, TexelEncoding::Rgba8)).starts_with("not a KTX2 texture"));
}

TEST_CASE("the header of a KTX2 texture gives its size without its texels") {
  const auto header = dwe::assets::inspectKtx2(uncompressedKtx2());
  REQUIRE(header.has_value());
  CHECK(header->width == 4);
  CHECK(header->height == 4);
  const std::vector<std::byte> cut(13, std::byte{0x5A});
  CHECK_FALSE(dwe::assets::inspectKtx2(cut).has_value());
  const std::vector<std::byte> garbage(256, std::byte{0x5A});
  CHECK_FALSE(dwe::assets::inspectKtx2(garbage).has_value());
}

TEST_CASE("the header of a KTX2 texture gives its shape, supercompression and level sizes") {
  const auto plain = inspectKtx2(ktx2Of({.width = 8, .height = 2}));
  REQUIRE(plain.has_value());
  CHECK(plain->dimensions == 2);
  CHECK(plain->depth == 1);
  CHECK(plain->layers == 1);
  CHECK(plain->faces == 1);
  CHECK(plain->levels == 1);
  CHECK(plain->supercompression == Ktx2Supercompression::None);
  CHECK_FALSE(plain->needsTranscoding);
  CHECK(plain->levelBytes == std::vector<std::uint64_t>{std::uint64_t{8} * 2 * 4});

  const auto basis = inspectKtx2(ktx2Of({.basis = true, .zstd = true}));
  REQUIRE(basis.has_value());
  CHECK(basis->supercompression == Ktx2Supercompression::Zstd);
  CHECK(basis->needsTranscoding);
  CHECK(checkPlain2dKtx2(*basis, 4).has_value());
  CHECK(decodeKtx2(ktx2Of({.basis = true, .zstd = true}), TexelEncoding::Rgba8).has_value());
}

// Each of these is refused from its header, by checkPlain2dKtx2 and by decodeKtx2 before it loads
// any texel.
TEST_CASE(
    "a KTX2 texture that is not one 2D image, or whose levels inflate past their size, is refused before load") {
  const auto refused = [](const std::vector<std::byte>& bytes, std::string_view why) {
    const auto header = inspectKtx2(bytes);
    REQUIRE(header.has_value());
    const auto checked = checkPlain2dKtx2(*header, dwe::assets::MaxImageSide);
    REQUIRE_FALSE(checked.has_value());
    CHECK(checked.error().message.contains(why));
    CHECK(errorOf(decodeKtx2(bytes, TexelEncoding::Rgba8)) == checked.error().message);
  };
  refused(ktx2Of({.layers = 2}), "not a plain 2D texture");
  refused(ktx2Of({.faces = 6}), "not a plain 2D texture");
  refused(ktx2Of({.width = 2, .height = 1}), "does not hold Basis Universal data");

  // A Zstd bomb: a 4x4 UASTC level whose index says it inflates to a terabyte.
  auto bomb = ktx2Of({.basis = true, .zstd = true});
  writeU64(bomb, FirstLevelInflatedAt, std::uint64_t{1} << 40);
  refused(
      bomb,
      "mip level 0 of the KTX2 texture takes 1099511627776 bytes once loaded, more than the 64 a 4x4");

  const auto header = inspectKtx2(ktx2Of({.width = 8, .height = 2}));
  REQUIRE(header.has_value());
  CHECK(checkPlain2dKtx2(*header, 4).error().message ==
        "the KTX2 texture is 8x2, not 1 to 4 on a side");
}

TEST_CASE("checkPlain2dKtx2 judges a header alone") {
  using dwe::assets::Ktx2Header;
  CHECK(checkPlain2dKtx2(plainHeader(), 4).has_value());
  const auto refusal = [](const Ktx2Header& header) {
    const auto checked = checkPlain2dKtx2(header, 4);
    REQUIRE_FALSE(checked.has_value());
    return checked.error().message;
  };
  const auto edited = [](auto edit) {
    Ktx2Header header = plainHeader();
    edit(header);
    return header;
  };
  CHECK(
      refusal(edited([](Ktx2Header& h) { h.dimensions = 3; })).contains("not a plain 2D texture"));
  CHECK(refusal(edited([](Ktx2Header& h) { h.depth = 2; })).contains("not a plain 2D texture"));
  CHECK(refusal(edited([](Ktx2Header& h) { h.layers = 2; })).contains("not a plain 2D texture"));
  CHECK(refusal(edited([](Ktx2Header& h) { h.faces = 6; })).contains("not a plain 2D texture"));
  CHECK(refusal(edited([](Ktx2Header& h) { h.width = 0; })).contains("not 1 to 4 on a side"));
  CHECK(refusal(edited([](Ktx2Header& h) { h.height = 5; })).contains("not 1 to 4 on a side"));
  CHECK(refusal(edited([](Ktx2Header& h) {
          h.levels = 4;
          h.levelBytes = {16, 16, 16, 16};
        })).contains("cannot have 4 mip levels"));
  CHECK(refusal(edited([](Ktx2Header& h) { h.levels = 0; })).contains("cannot have 0 mip levels"));
  CHECK(refusal(edited([](Ktx2Header& h) {
          h.supercompression = Ktx2Supercompression::Other;
        })).contains("other than BasisLZ or Zstd"));
  CHECK(refusal(edited([](Ktx2Header& h) {
          h.levelBytes = {65};
        })).contains("takes 65 bytes once loaded"));
  CHECK(refusal(edited([](Ktx2Header& h) {
          h.needsTranscoding = false;
        })).contains("Basis Universal"));
  // BasisLZ levels load as stored: the file bounds them, not their size.
  CHECK(checkPlain2dKtx2(edited([](Ktx2Header& h) {
                           h.supercompression = Ktx2Supercompression::BasisLz;
                           h.levelBytes = {1000};
                         }),
                         4)
            .has_value());
}

TEST_CASE(
    "a KTX2 header whose supercompression global data runs past the file is refused before libktx reads it") {
  auto bytes = uncompressedKtx2();
  writeU64(bytes, SgdLengthAt, std::uint64_t{1} << 40);
  const auto header = inspectKtx2(bytes);
  REQUIRE_FALSE(header.has_value());
  CHECK(
      header.error().message.contains("its supercompression global data (1099511627776 bytes at"));
  CHECK(errorOf(decodeKtx2(bytes, TexelEncoding::Rgba8)) == header.error().message);
}
