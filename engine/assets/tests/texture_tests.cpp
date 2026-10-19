#include <dwe/assets/texture.hpp>

#include <doctest/doctest.h>
#include <ktx.h>

#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

using dwe::assets::decodeKtx2;
using dwe::assets::TexelEncoding;

namespace {

// A 4x4 KTX2 texture with one uncompressed RGBA8 level, written by libktx.
std::vector<std::byte> uncompressedKtx2() {
    ktxTextureCreateInfo info{};
    info.vkFormat = 37; // VK_FORMAT_R8G8B8A8_UNORM
    info.baseWidth = 4;
    info.baseHeight = 4;
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

std::string errorOf(const dwe::core::Result<dwe::assets::TextureData>& result) {
    return result ? std::string{} : result.error().message;
}

} // namespace

TEST_CASE("the KTX2 decoder refuses what is not a KTX2 texture") {
    CHECK(errorOf(decodeKtx2({}, TexelEncoding::Rgba8)).starts_with("not a KTX2 texture"));
    const std::vector<std::byte> cut(13, std::byte{0x5A});
    CHECK(errorOf(decodeKtx2(cut, TexelEncoding::Rgba8)) == "not a KTX2 texture: 13 bytes is shorter than its header");
    const std::vector<std::byte> garbage(256, std::byte{0x5A});
    CHECK(errorOf(decodeKtx2(garbage, TexelEncoding::Bc7)).starts_with("not a KTX2 texture"));
}

TEST_CASE("the KTX2 decoder takes only Basis Universal data") {
    const auto bytes = uncompressedKtx2();
    CHECK(errorOf(decodeKtx2(bytes, TexelEncoding::Rgba8)) == "the KTX2 texture does not hold Basis Universal data");
}

TEST_CASE("a truncated KTX2 texture is refused") {
    auto bytes = uncompressedKtx2();
    bytes.resize(bytes.size() - 1);
    CHECK(errorOf(decodeKtx2(bytes, TexelEncoding::Rgba8)).starts_with("not a KTX2 texture"));
}
