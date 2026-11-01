#include <dwe/assets/image_palettes.hpp>
#include <dwe/assets/map_images.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <utility>
#include <vector>

#include "messages.hpp"

namespace dwe::assets {

namespace {

core::Result<std::int32_t> readInt(const core::YamlNode& node, std::string_view key) {
  const core::YamlNode* value = node.find(key);
  if (value == nullptr || !value->isScalar()) {
    return core::fail(std::format("texture metadata: '{}' is missing", key));
  }
  std::int32_t number = 0;
  const std::string& text = value->text();
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return core::fail(std::format("texture metadata: '{}' is not a number", key));
  }
  return number;
}

core::Result<std::vector<PaletteColor>> readClut(const core::YamlNode& node) {
  if (!node.isScalar()) {
    return core::fail(std::string{messages::PaletteNotText});
  }
  auto values = parseOriginalPalette(node.text());
  if (!values) {
    return std::unexpected{values.error()};
  }
  std::vector<PaletteColor> colors;
  colors.reserve(values->size());
  for (const std::uint16_t value : *values) {
    colors.push_back(colorFromOriginal(value));
  }
  return colors;
}

core::Result<IndexedImage> readIndexed(const VirtualFileSystem& vfs, const AssetId& id) {
  auto bytes = vfs.read(id);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  auto image = decodeIndexedPng(*bytes);
  if (!image) {
    return core::fail(std::format("{}: {}", id.str(), image.error().message));
  }
  return image;
}

} // namespace

PaletteColor colorFromOriginal(std::uint16_t value) {
  if (value == 0) {
    return {};
  }
  const auto channel = [value](int shift) {
    const auto bits = static_cast<std::uint8_t>((value >> shift) & 0x1F);
    return static_cast<std::uint8_t>((bits << 3) | (bits >> 2));
  };
  constexpr std::uint16_t SemiTransparencyBit = 0x8000;
  const std::uint8_t alpha = (value & SemiTransparencyBit) != 0 ? SemiTransparentAlpha : 255;
  return {.red = channel(0), .green = channel(5), .blue = channel(10), .alpha = alpha};
}

std::vector<PaletteColor> paletteFromPng(std::vector<PaletteColor> palette) {
  constexpr PaletteColor OpaqueBlack{.red = 0, .green = 0, .blue = 0, .alpha = 255};
  for (PaletteColor& color : palette) {
    if (color == OpaqueBlack) {
      color.alpha = SemiTransparentAlpha;
    }
  }
  return palette;
}

core::Result<MapTexturePage> parseMapTexturePage(std::int32_t bitsPerPixel, IndexedImage image,
                                                 std::string_view metaYaml) {
  auto root = core::parseYaml(metaYaml);
  if (!root) {
    return std::unexpected{root.error()};
  }
  const core::YamlNode* imageRect = root->find("image_rect");
  const core::YamlNode* clutRect = root->find("clut_rect");
  const core::YamlNode* palettes = root->find("palettes");
  if (imageRect == nullptr || clutRect == nullptr || palettes == nullptr ||
      !palettes->isSequence()) {
    return core::fail("texture metadata: image_rect, clut_rect or palettes is missing");
  }
  auto vramX = readInt(*imageRect, "x");
  auto vramY = readInt(*imageRect, "y");
  auto clutRow = readInt(*clutRect, "y");
  for (const auto* value : {&vramX, &vramY, &clutRow}) {
    if (!*value) {
      return std::unexpected{value->error()};
    }
  }
  MapTexturePage page{
      .bitsPerPixel = bitsPerPixel,
      .vramX = *vramX,
      .vramY = *vramY,
      .clutRow = *clutRow,
      .image = std::move(image),
      .cluts = {},
  };
  for (const core::YamlNode& palette : palettes->items()) {
    auto colors = readClut(palette);
    if (!colors) {
      return std::unexpected{colors.error()};
    }
    page.cluts.push_back(std::move(*colors));
  }
  return page;
}

SpriteBlend spriteBlend(std::int32_t transparency) {
  switch (transparency) {
  case 0:
    return SpriteBlend::Average;
  case 1:
    return SpriteBlend::Add;
  case 2:
    return SpriteBlend::Subtract;
  case 3:
    return SpriteBlend::AddQuarter;
  default:
    return SpriteBlend::Opaque;
  }
}

std::int32_t mapSpriteOrderValue(const MapSprite& sprite, float viewDepth) {
  if (sprite.position.y == MapOrder::BehindEverythingY) {
    return MapOrder::BehindEverything;
  }
  const auto order = static_cast<std::int32_t>(std::floor(viewDepth / MapOrderingTableStep));
  if (order < MapOrder::Nearest) {
    return MapOrder::Nearest;
  }
  return order >= MapOrder::Limit ? MapOrder::Farthest : order;
}

const std::vector<PaletteColor>*
mapClutPalette(std::int32_t clut, std::span<const MapTexturePage> pages,
               std::span<const std::vector<PaletteColor>> backgroundPalettes) {
  using Layout = MapVramLayout;
  if (clut < 0) {
    return backgroundPalettes.empty() ? nullptr : &backgroundPalettes.front();
  }
  if (clut < Layout::FourBitCluts) {
    const auto firstFourBit = std::ranges::find_if(
        pages, [](const MapTexturePage& page) { return page.bitsPerPixel == 4; });
    if (firstFourBit == pages.end() || std::cmp_greater_equal(clut, firstFourBit->cluts.size())) {
      return nullptr;
    }
    return &firstFourBit->cluts[static_cast<std::size_t>(clut)];
  }
  const std::int32_t row = clut + Layout::ClutRowBase;
  // loadMapImage1 uploads an 8-bit page's CLUT unless it is for the time of day's row.
  for (const MapTexturePage& page : pages) {
    if (page.bitsPerPixel == 8 && page.clutRow == row && row != Layout::TimeOfDayRow &&
        !page.cluts.empty()) {
      return &page.cluts.front();
    }
  }
  const std::int32_t background = row - Layout::FirstBackgroundPaletteRow;
  if (background >= 0 && std::cmp_less(background, backgroundPalettes.size())) {
    return &backgroundPalettes[static_cast<std::size_t>(background)];
  }
  return nullptr;
}

MapSpriteSheet buildMapSpriteSheet(std::span<const MapSprite> sprites,
                                   std::span<const MapTexturePage> pages,
                                   std::span<const std::vector<PaletteColor>> backgroundPalettes) {
  using Layout = MapVramLayout;
  // The fixed palette a CLUT reference reads, or nullptr when nothing was uploaded there. clut -1
  // is not fixed.
  const auto fixedPalette = [&](std::int32_t clut) -> const std::vector<PaletteColor>* {
    return mapClutPalette(clut, pages, backgroundPalettes);
  };

  MapSpriteSheet sheet;
  // One texture per page and palette (nullptr: the time of day's).
  std::map<std::pair<std::size_t, const void*>, std::size_t> built;
  for (const MapSprite& sprite : sprites) {
    MapSpriteSheet::Placement placement{
        .texture = MapSpriteSheet::NoTexture, .texel = {}, .size = {}};
    const bool timeOfDay = sprite.clut < 0;
    const bool fourBit = !timeOfDay && sprite.clut < Layout::FourBitCluts;
    const std::int32_t bitsPerPixel = fourBit ? 4 : 8;
    const std::int32_t pageWidth = fourBit ? Layout::FourBitPageWidth : Layout::EightBitPageWidth;
    const std::int32_t vramX = Layout::FirstPageX + ((sprite.texX / MapPageTexels) * pageWidth);
    const auto page = std::ranges::find_if(pages, [&](const MapTexturePage& candidate) {
      return candidate.bitsPerPixel == bitsPerPixel && candidate.vramX == vramX;
    });
    const std::vector<PaletteColor>* palette = timeOfDay ? nullptr : fixedPalette(sprite.clut);
    const bool resolved = timeOfDay ? !backgroundPalettes.empty() : palette != nullptr;
    if (page != pages.end() && resolved && sprite.texX >= 0 && sprite.texY >= 0) {
      const auto pageIndex = static_cast<std::size_t>(page - pages.begin());
      const auto key = std::pair{pageIndex, static_cast<const void*>(palette)};
      auto found = built.find(key);
      if (found == built.end()) {
        MapSpriteSheet::Texture texture;
        texture.timeOfDayPalette = timeOfDay;
        if (timeOfDay) {
          texture.indices = {.width = page->image.width,
                             .height = page->image.height,
                             .indices = page->image.indices,
                             .palette = {}};
        } else {
          texture.colors = applyPalette(page->image, *palette);
        }
        sheet.textures.push_back(std::move(texture));
        found = built.emplace(key, sheet.textures.size() - 1).first;
      }
      placement = {
          .texture = found->second,
          .texel = {sprite.texX % MapPageTexels, sprite.texY % MapPageTexels},
          .size = {sprite.width, sprite.height},
      };
    }
    sheet.placements.push_back(placement);
  }
  return sheet;
}

namespace {

// The map's part of VRAM, in 16-bit units: MapVramLayout::FirstPageX to the right edge, rows 0 to
// MapPageTexels. loadMap uploads the 8-bit entries, then the 4-bit ones, each with one LoadImage at
// its image_rect (overworld.c: loadMapImage1, loadMapImage2), so an entry overwrites what an
// earlier one put under it, whatever their depths, and a page reads whatever entries cover it.
class MapVram {
public:
  // VRAM is 1024 16-bit units wide.
  static constexpr std::int32_t VramWidth = 1024;
  static constexpr std::int32_t Left = MapVramLayout::FirstPageX;
  static constexpr std::int32_t Width = VramWidth - Left;
  static constexpr std::int32_t Rows = MapPageTexels;

  core::Result<void> upload(const MapTexturePage& entry) {
    const std::int32_t perUnit = texelsPerUnit(entry.bitsPerPixel);
    const auto texels = static_cast<std::int32_t>(entry.image.width);
    const auto rows = static_cast<std::int32_t>(entry.image.height);
    const std::int32_t left = entry.vramX - Left;
    if (perUnit == 0 || texels % perUnit != 0 || left < 0 || left + (texels / perUnit) > Width ||
        entry.vramY < 0 || entry.vramY + rows > Rows) {
      return core::fail(std::format(
          "{}x{} texels of {} bits at VRAM ({}, {}) do not fit the map's VRAM from x {}", texels,
          rows, entry.bitsPerPixel, entry.vramX, entry.vramY, Left));
    }
    const auto mask = static_cast<std::uint32_t>((1U << entry.bitsPerPixel) - 1);
    for (std::int32_t row = 0; row < rows; ++row) {
      for (std::int32_t texel = 0; texel < texels; ++texel) {
        std::uint16_t& unit = at(left + (texel / perUnit), entry.vramY + row);
        const auto shift = static_cast<std::uint32_t>((texel % perUnit) * entry.bitsPerPixel);
        const std::uint32_t index =
            entry.image.indices[static_cast<std::size_t>((row * texels) + texel)] & mask;
        const std::uint32_t kept = static_cast<std::uint32_t>(unit) & ~(mask << shift);
        unit = static_cast<std::uint16_t>(kept | (index << shift));
      }
    }
    return {};
  }

  // The page of the given depth whose left edge is at vramX, MapPageTexels square.
  [[nodiscard]] std::vector<std::uint8_t> page(std::int32_t bitsPerPixel,
                                               std::int32_t vramX) const {
    const std::int32_t perUnit = texelsPerUnit(bitsPerPixel);
    const auto mask = static_cast<std::uint32_t>((1U << bitsPerPixel) - 1);
    std::vector<std::uint8_t> indices(static_cast<std::size_t>(MapPageTexels) * MapPageTexels);
    for (std::int32_t row = 0; row < Rows; ++row) {
      for (std::int32_t texel = 0; texel < MapPageTexels; ++texel) {
        const std::int32_t column = vramX - Left + (texel / perUnit);
        if (column >= Width) {
          break;
        }
        const auto shift = static_cast<std::uint32_t>((texel % perUnit) * bitsPerPixel);
        const auto unit = static_cast<std::uint32_t>(units_[unitIndex(column, row)]);
        indices[static_cast<std::size_t>((row * MapPageTexels) + texel)] =
            static_cast<std::uint8_t>((unit >> shift) & mask);
      }
    }
    return indices;
  }

private:
  static constexpr std::int32_t texelsPerUnit(std::int32_t bitsPerPixel) {
    return bitsPerPixel == 4 || bitsPerPixel == 8 ? 16 / bitsPerPixel : 0;
  }
  static std::size_t unitIndex(std::int32_t column, std::int32_t row) {
    return static_cast<std::size_t>((row * Width) + column);
  }
  std::uint16_t& at(std::int32_t column, std::int32_t row) {
    return units_[unitIndex(column, row)];
  }

  std::vector<std::uint16_t> units_ =
      std::vector<std::uint16_t>(static_cast<std::size_t>(Width) * Rows);
};

// The pages sprites look up (MapVramLayout: from FirstPageX, a page width apart) that an entry
// covers, read back from VRAM with the entry's CLUTs.
void addPagesOf(const MapTexturePage& entry, const MapVram& vram,
                std::vector<MapTexturePage>& pages) {
  const std::int32_t pageWidth =
      entry.bitsPerPixel == 4 ? MapVramLayout::FourBitPageWidth : MapVramLayout::EightBitPageWidth;
  const std::int32_t units = static_cast<std::int32_t>(entry.image.width) * entry.bitsPerPixel / 16;
  const std::int32_t first = (entry.vramX - MapVram::Left) / pageWidth;
  const std::int32_t last = (entry.vramX - MapVram::Left + units - 1) / pageWidth;
  for (std::int32_t index = first; index <= last; ++index) {
    const std::int32_t vramX = MapVram::Left + (index * pageWidth);
    pages.push_back({
        .bitsPerPixel = entry.bitsPerPixel,
        .vramX = vramX,
        .vramY = 0,
        .clutRow = entry.clutRow,
        .image = {.width = MapPageTexels,
                  .height = MapPageTexels,
                  .indices = vram.page(entry.bitsPerPixel, vramX),
                  .palette = entry.image.palette},
        .cluts = entry.cluts,
    });
  }
}

} // namespace

core::Result<MapImages> loadMapImages(const VirtualFileSystem& vfs, const MapDocument& document) {
  const MapAssetIds& ids = document.assets;
  if (ids.backgrounds.empty()) {
    return core::fail(std::format("map {} has no background", document.id));
  }
  const auto expectedWidth =
      static_cast<std::uint32_t>(std::max(document.tiles.width, 0) * MapTileSize);
  const auto expectedHeight =
      static_cast<std::uint32_t>(std::max(document.tiles.height, 0) * MapTileSize);
  MapImages images;
  for (const MapBackground& background : ids.backgrounds) {
    auto image = readIndexed(vfs, background.id);
    if (!image) {
      return std::unexpected{image.error()};
    }
    if (image->width != expectedWidth || image->height != expectedHeight) {
      return core::fail(std::format("{}: {}x{} pixels, but the map has {}x{} tiles of {}",
                                    background.id.str(), image->width, image->height,
                                    document.tiles.width, document.tiles.height, MapTileSize));
    }
    if (images.backgroundPalettes.empty()) {
      images.background = std::move(*image);
      images.backgroundPalettes.push_back(paletteFromPng(images.background.palette));
    } else if (image->indices != images.background.indices) {
      return core::fail(
          std::format("{}: its pixels differ from the first background's", background.id.str()));
    } else {
      // The variants are the same tiles with another palette (docs/formats/mapas.md:
      // composeBackgrounds).
      images.backgroundPalettes.push_back(paletteFromPng(std::move(image->palette)));
    }
    images.backgroundVariants.push_back(background.variant);
  }
  // ids.textures lists the 8-bit entries first, as loadMap uploads them.
  MapVram vram;
  std::vector<MapTexturePage> entries;
  for (const MapTexture& texture : ids.textures) {
    if (!texture.meta) {
      return core::fail(std::format("{}: no metadata", texture.image.str()));
    }
    auto image = readIndexed(vfs, texture.image);
    if (!image) {
      return std::unexpected{image.error()};
    }
    auto meta = vfs.read(*texture.meta);
    if (!meta) {
      return std::unexpected{meta.error()};
    }
    const std::string_view text{reinterpret_cast<const char*>(meta->data()), meta->size()};
    auto page = parseMapTexturePage(texture.bitsPerPixel, std::move(*image), text);
    if (!page) {
      return core::fail(std::format("{}: {}", texture.meta->str(), page.error().message));
    }
    if (auto uploaded = vram.upload(*page); !uploaded) {
      return core::fail(std::format("{}: {}", texture.image.str(), uploaded.error().message));
    }
    entries.push_back(std::move(*page));
  }
  for (const MapTexturePage& entry : entries) {
    addPagesOf(entry, vram, images.pages);
  }
  const std::span<const MapSprite> sprites =
      document.objects ? std::span{document.objects->sprites} : std::span<const MapSprite>{};
  images.sprites = buildMapSpriteSheet(sprites, images.pages, images.backgroundPalettes);
  return images;
}

} // namespace dwe::assets
