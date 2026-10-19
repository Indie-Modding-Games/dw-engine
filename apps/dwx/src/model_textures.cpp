#include "model_textures.hpp"

#include <dwe/formats/image/png.hpp>

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <tuple>
#include <utility>

namespace dwe::dwx {

namespace {

using formats::image::Color15;
using formats::image::Tim;
using formats::image::TimPixelMode;
using formats::model::TmdPrimitive;

constexpr std::uint16_t PageTexels = 256;

// The TSB colour depth that reads a TIM's pixels, if a texture page can read them.
std::optional<std::uint8_t> depthOf(const Tim& tim) {
    switch (tim.mode) {
    case TimPixelMode::Indexed4:
        return 0;
    case TimPixelMode::Indexed8:
        return 1;
    case TimPixelMode::Direct16:
        return formats::model::TmdDirectColorDepth;
    case TimPixelMode::Direct24:
        return std::nullopt;
    }
    return std::nullopt;
}

std::size_t paletteSize(std::uint8_t depth) {
    return depth == 0 ? 16 : 256;
}

// The texels a primitive's UVs reach, in texel coordinates across VRAM at its colour depth. The right and bottom
// ends may be one past the last texel, as UVs of a polygon's far edge are.
struct TexelBox {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

TexelBox texelBoxOf(const TmdPrimitive& primitive) {
    const int texelsPerHalfword = 4 >> primitive.depth();
    const auto corners = std::span(primitive.uvs).first(primitive.vertexCount);
    const auto [minU, maxU] = std::ranges::minmax(corners | std::views::transform(&formats::model::TmdUv::u));
    const auto [minV, maxV] = std::ranges::minmax(corners | std::views::transform(&formats::model::TmdUv::v));
    const int pageX = primitive.pageX() * texelsPerHalfword;
    return {.left = pageX + minU,
            .top = primitive.pageY() + minV,
            .right = pageX + maxU,
            .bottom = primitive.pageY() + maxV};
}

// The latest image of the primitive's depth that holds its texels.
std::optional<std::size_t> findImage(const VramScene& scene, const TmdPrimitive& primitive, const TexelBox& box) {
    const auto& images = scene.images();
    for (std::size_t i = images.size(); i-- > 0;) {
        const Tim& tim = images[i];
        if (depthOf(tim) != primitive.depth()) {
            continue;
        }
        const int left = tim.imageRect.x * (4 >> primitive.depth());
        const int top = tim.imageRect.y;
        const int width = static_cast<int>(formats::image::timWidth(tim));
        if (box.left >= left && box.right <= left + width && box.top >= top &&
            box.bottom <= top + tim.imageRect.height) {
            return i;
        }
    }
    return std::nullopt;
}

// The latest palette that holds every colour the primitive's depth can index at its CLUT position.
std::optional<std::size_t> findPalette(const VramScene& scene, const TmdPrimitive& primitive) {
    const auto& palettes = scene.palettes();
    const std::size_t needed = paletteSize(primitive.depth());
    for (std::size_t i = palettes.size(); i-- > 0;) {
        const VramScene::Palette& palette = palettes[i];
        if (palette.y == primitive.clutY() && palette.x <= primitive.clutX() &&
            primitive.clutX() + needed <= palette.x + palette.colors.size()) {
            return i;
        }
    }
    return std::nullopt;
}

// An image drawn with one palette: the palette's index in the scene and the CLUT x the primitives point at.
using PaletteUse = std::pair<std::size_t, std::uint16_t>;

struct Resolved {
    std::size_t image = 0;
    std::optional<PaletteUse> palette; // none for 16-bit images
};

std::span<const Color15> colorsOf(const VramScene& scene, const PaletteUse& use, std::uint8_t depth) {
    const VramScene::Palette& palette = scene.palettes()[use.first];
    return std::span(palette.colors).subspan(use.second - palette.x, paletteSize(depth));
}

// Appends the colours of the texels inside a primitive's box.
void sampleTexels(std::vector<formats::image::Rgba8>& texels, const VramScene& scene, const Resolved& resolved,
                  const std::vector<std::uint8_t>& indices, const TmdPrimitive& primitive, const TexelBox& box) {
    const Tim& tim = scene.images()[resolved.image];
    const int width = static_cast<int>(formats::image::timWidth(tim));
    const int height = tim.imageRect.height;
    const int left = tim.imageRect.x * (4 >> primitive.depth());
    const int top = tim.imageRect.y;
    const auto colors =
        resolved.palette ? colorsOf(scene, *resolved.palette, primitive.depth()) : std::span<const Color15>{};
    for (int y = box.top - top; y <= std::min(box.bottom - top, height - 1); ++y) {
        for (int x = box.left - left; x <= std::min(box.right - left, width - 1); ++x) {
            const std::size_t at =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x);
            if (resolved.palette) {
                texels.push_back(formats::image::toRgba8(colors[indices[at]]));
            } else {
                texels.push_back(formats::image::toRgba8(
                    static_cast<Color15>(std::to_integer<unsigned>(tim.data[at * 2]) |
                                         (std::to_integer<unsigned>(tim.data[(at * 2) + 1]) << 8U))));
            }
        }
    }
}

core::Result<std::vector<std::byte>> drawImage(const VramScene& scene, const Tim& tim,
                                               const std::vector<std::uint8_t>& indices,
                                               const std::optional<PaletteUse>& palette, std::uint8_t depth) {
    if (!palette) {
        const auto image = formats::image::decodeTim(tim);
        if (!image) {
            return std::unexpected{image.error()};
        }
        return formats::image::encodePngRgba(*image);
    }
    std::vector<formats::image::Rgba8> colors;
    std::ranges::transform(colorsOf(scene, *palette, depth), std::back_inserter(colors), formats::image::toRgba8);
    return formats::image::encodePngIndexed(formats::image::timWidth(tim), tim.imageRect.height, indices, colors);
}

} // namespace

void VramScene::addTim(const Tim& tim) {
    addPixels(tim);
    if (!tim.hasClut) {
        return;
    }
    const std::size_t width = tim.clutRect.width;
    for (std::size_t row = 0; row < tim.clutRect.height; ++row) {
        const auto begin = tim.clut.begin() + static_cast<std::ptrdiff_t>(row * width);
        addPalette(tim.clutRect.x, static_cast<std::uint16_t>(tim.clutRect.y + row),
                   std::vector<Color15>(begin, begin + static_cast<std::ptrdiff_t>(width)));
    }
}

void VramScene::addPixels(const Tim& tim) {
    images_.push_back(tim);
}

void VramScene::addPalette(std::uint16_t x, std::uint16_t y, std::vector<Color15> colors) {
    palettes_.push_back({.x = x, .y = y, .colors = std::move(colors)});
}

core::Result<ModelTextures> textureModel(const formats::model::Tmd& tmd, const VramScene& scene) {
    ModelTextures result;
    // What each used image is drawn with, by image; the 16-bit ones with no palette.
    std::map<std::size_t, std::set<std::optional<PaletteUse>>> uses;
    std::map<std::size_t, std::vector<std::uint8_t>> indicesOf;
    // Unresolved primitives by page x, page y, depth, CLUT x, CLUT y and reason.
    std::map<std::tuple<int, int, int, int, int, std::string_view>, std::size_t> unresolved;

    for (const auto& object : tmd.objects) {
        for (const TmdPrimitive& primitive : object.primitives) {
            if (!primitive.isTextured()) {
                continue;
            }
            ++result.texturedPrimitives;
            const TexelBox box = texelBoxOf(primitive);
            const bool direct = primitive.depth() == formats::model::TmdDirectColorDepth;
            const auto image = findImage(scene, primitive, box);
            const auto palette = image && !direct ? findPalette(scene, primitive) : std::nullopt;
            if (!image || (!direct && !palette)) {
                const std::string_view reason = image ? "no palette is there" : "no image holds their texels";
                ++unresolved[{primitive.pageX(), primitive.pageY(), primitive.depth(), primitive.clutX(),
                              primitive.clutY(), reason}];
                continue;
            }
            const std::size_t imageIndex = *image;
            const Resolved resolved{.image = imageIndex,
                                    .palette = palette ? std::optional{PaletteUse{*palette, primitive.clutX()}}
                                                       : std::nullopt};
            auto& indices = indicesOf[imageIndex];
            if (palette && indices.empty()) {
                auto read = formats::image::timIndices(scene.images()[imageIndex]);
                if (!read) {
                    return std::unexpected{read.error()};
                }
                indices = std::move(*read);
            }
            uses[imageIndex].insert(resolved.palette);
            sampleTexels(result.texels, scene, resolved, indices, primitive, box);
            ++result.resolvedPrimitives;
        }
    }

    // Latest uploads first: the glTF writer takes the first texture that holds a primitive's texels and palette.
    for (const auto& [image, palettes] : std::views::reverse(uses)) {
        const Tim& tim = scene.images()[image];
        // findImage takes only images a page can read, so every used image has a depth.
        const std::uint8_t depth = depthOf(tim).value_or(0);
        formats::model::ModelTexture texture{.pixelX = tim.imageRect.x,
                                             .pixelY = tim.imageRect.y,
                                             .width = static_cast<std::uint16_t>(formats::image::timWidth(tim)),
                                             .height = tim.imageRect.height,
                                             .depth = depth,
                                             .palettes = {}};
        for (const auto& palette : palettes) {
            auto png = drawImage(scene, tim, indicesOf[image], palette, depth);
            if (!png) {
                return std::unexpected{png.error()};
            }
            const auto& stored = result.pngs.emplace_back(std::move(*png));
            const VramScene::Palette* at = palette ? &scene.palettes()[palette->first] : nullptr;
            texture.palettes.push_back({.clutX = palette ? palette->second : std::uint16_t{0},
                                        .clutY = at != nullptr ? at->y : std::uint16_t{0},
                                        .png = stored});
        }
        result.textures.push_back(std::move(texture));
    }

    // The rest keep their UVs on their page.
    std::set<std::tuple<int, int, int>> pages;
    for (const auto& [key, count] : unresolved) {
        const auto& [pageX, pageY, depth, clutX, clutY, reason] = key;
        result.unresolved.push_back(std::format("{} textured primitive{} read the {}-bit page at ({}, {}) with the "
                                                "palette at ({}, {}), but {}",
                                                count, count == 1 ? "" : "s", 4 << depth, pageX, pageY, clutX, clutY,
                                                reason));
        if (pages.emplace(pageX, pageY, depth).second) {
            result.textures.push_back({.pixelX = static_cast<std::uint16_t>(pageX),
                                       .pixelY = static_cast<std::uint16_t>(pageY),
                                       .width = PageTexels,
                                       .height = PageTexels,
                                       .depth = static_cast<std::uint8_t>(depth),
                                       .palettes = {}});
        }
    }
    return result;
}

} // namespace dwe::dwx
