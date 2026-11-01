#include "sign_dump.hpp"

#include <dwe/assets/image.hpp>
#include <dwe/assets/map_images.hpp>
#include <dwe/core/log.hpp>
#include <dwe/localization/catalog.hpp>
#include <dwe/render/map_camera.hpp>
#include <dwe/render/map_scene.hpp>
#include <dwe/signs/map_signs.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "content_mount.hpp"

namespace dwe::app {
namespace {

constexpr std::int32_t GridStep = 16;
constexpr std::int32_t GridMajorStep = 64;
constexpr std::string_view DefaultLanguage = "en";

bool writeImage(const std::filesystem::path& path, const assets::Image& image) {
  const auto png = assets::encodePng(image);
  if (!png) {
    core::logError("dump-signs: {}: {}", path.string(), png.error().message);
    return false;
  }
  std::ofstream stream{path, std::ios::binary};
  stream.write(reinterpret_cast<const char*>(png->data()),
               static_cast<std::streamsize>(png->size()));
  return stream.good();
}

void blend(assets::Image& image, std::int32_t x, std::int32_t y, std::array<std::uint8_t, 3> color,
           float amount) {
  if (x < 0 || y < 0 || std::cmp_greater_equal(x, image.width) ||
      std::cmp_greater_equal(y, image.height)) {
    return;
  }
  const std::size_t at =
      ((static_cast<std::size_t>(y) * image.width) + static_cast<std::size_t>(x)) * 4;
  for (std::size_t channel = 0; channel < 3; ++channel) {
    const auto old = static_cast<float>(image.pixels[at + channel]);
    image.pixels[at + channel] = static_cast<std::byte>(
        std::lround(std::lerp(old, static_cast<float>(color.at(channel)), amount)));
  }
  image.pixels[at + 3] = std::byte{255};
}

// A copy with grid lines every GridStep pixels (stronger every GridMajorStep) and the regions
// outlined, to read coordinates off the image.
assets::Image withGrid(const assets::Image& source, const std::vector<signs::PixelRect>& regions) {
  assets::Image image = source;
  for (std::int32_t y = 0; std::cmp_less(y, image.height); ++y) {
    for (std::int32_t x = 0; std::cmp_less(x, image.width); ++x) {
      if (x % GridMajorStep == 0 || y % GridMajorStep == 0) {
        blend(image, x, y, {255, 0, 255}, 0.7F);
      } else if (x % GridStep == 0 || y % GridStep == 0) {
        blend(image, x, y, {255, 0, 255}, 0.3F);
      }
    }
  }
  for (const signs::PixelRect& rect : regions) {
    for (std::int32_t x = rect.x - 1; x <= rect.x + rect.width; ++x) {
      blend(image, x, rect.y - 1, {0, 255, 255}, 1.0F);
      blend(image, x, rect.y + rect.height, {0, 255, 255}, 1.0F);
    }
    for (std::int32_t y = rect.y - 1; y <= rect.y + rect.height; ++y) {
      blend(image, rect.x - 1, y, {0, 255, 255}, 1.0F);
      blend(image, rect.x + rect.width, y, {0, 255, 255}, 1.0F);
    }
  }
  return image;
}

std::int32_t pageNumber(const assets::MapTexturePage& page) {
  const std::int32_t width = page.bitsPerPixel == 4 ? assets::MapVramLayout::FourBitPageWidth
                                                    : assets::MapVramLayout::EightBitPageWidth;
  return (page.vramX - assets::MapVramLayout::FirstPageX) / width;
}

struct PageKey {
  std::int32_t bits;
  std::int32_t page;
  auto operator<=>(const PageKey&) const = default;
};

// The palettes the sprites read each page with.
std::map<PageKey, std::set<std::int32_t>> pageCluts(std::span<const assets::MapSprite> sprites) {
  std::map<PageKey, std::set<std::int32_t>> cluts;
  for (const assets::MapSprite& sprite : sprites) {
    const bool fourBit = sprite.clut >= 0 && sprite.clut < assets::MapVramLayout::FourBitCluts;
    cluts[{.bits = fourBit ? 4 : 8, .page = sprite.texX / assets::MapPageTexels}].insert(
        sprite.clut);
  }
  return cluts;
}

std::vector<signs::PixelRect> regionsOn(const signs::SignOverlay& overlay,
                                        signs::SignTarget::Kind kind, std::int32_t bits,
                                        std::int32_t page) {
  std::vector<signs::PixelRect> regions;
  for (const signs::Sign& sign : overlay.signs) {
    if (sign.target.kind != kind) {
      continue;
    }
    if (kind == signs::SignTarget::Kind::Page &&
        (sign.target.bitsPerPixel != bits || sign.target.page != page)) {
      continue;
    }
    regions.push_back(sign.region);
  }
  return regions;
}

bool dumpImages(const std::filesystem::path& folder, const std::string& prefix,
                const assets::MapImages& images, std::span<const assets::MapSprite> sprites,
                const signs::SignOverlay& overlay, bool grids) {
  bool ok = true;
  const assets::Image background =
      assets::applyPalette(images.background, images.backgroundPalettes.front());
  ok = writeImage(folder / std::format("{}-background.png", prefix), background) && ok;
  if (grids) {
    ok = writeImage(
             folder / std::format("{}-background-grid.png", prefix),
             withGrid(background, regionsOn(overlay, signs::SignTarget::Kind::Background, 0, 0))) &&
         ok;
  }
  const auto cluts = pageCluts(sprites);
  for (const assets::MapTexturePage& page : images.pages) {
    const std::int32_t number = pageNumber(page);
    std::set<std::int32_t> used;
    if (const auto found = cluts.find({.bits = page.bitsPerPixel, .page = number});
        found != cluts.end()) {
      used = found->second;
    }
    // Pages no sprite reads still show, with the CLUT that came with them.
    std::vector<std::pair<std::string, const std::vector<assets::PaletteColor>*>> palettes;
    for (const std::int32_t clut : used) {
      if (const auto* palette =
              assets::mapClutPalette(clut, images.pages, images.backgroundPalettes)) {
        palettes.emplace_back(std::format("clut{}", clut), palette);
      }
    }
    if (palettes.empty() && !page.cluts.empty()) {
      palettes.emplace_back("own", &page.cluts.front());
    }
    for (const auto& [name, palette] : palettes) {
      const assets::Image colors = assets::applyPalette(page.image, *palette);
      const std::string stem =
          std::format("{}-page{}-{}-{}", prefix, page.bitsPerPixel, number, name);
      ok = writeImage(folder / std::format("{}.png", stem), colors) && ok;
      if (grids) {
        ok = writeImage(folder / std::format("{}-grid.png", stem),
                        withGrid(colors, regionsOn(overlay, signs::SignTarget::Kind::Page,
                                                   page.bitsPerPixel, number))) &&
             ok;
      }
    }
  }
  return ok;
}

void writeSprites(const std::filesystem::path& path, const assets::MapObjects& objects) {
  const std::span<const assets::MapSprite> sprites{objects.sprites};
  std::ofstream stream{path};
  stream << "# sprite: page <bits>-<number>, clut, texels [x, y, width, height] in the page\n";
  for (std::size_t index = 0; index < sprites.size(); ++index) {
    const assets::MapSprite& sprite = sprites[index];
    const bool fourBit = sprite.clut >= 0 && sprite.clut < assets::MapVramLayout::FourBitCluts;
    stream << std::format("{}: page {}-{} clut {} texels [{}, {}, {}, {}]\n", index,
                          fourBit ? 4 : 8, sprite.texX / assets::MapPageTexels, sprite.clut,
                          sprite.texX % assets::MapPageTexels, sprite.texY % assets::MapPageTexels,
                          sprite.width, sprite.height);
  }
  stream << "# instance: where its sprites go on the background, and their indices\n";
  for (std::size_t index = 0; index < objects.instances.size(); ++index) {
    const assets::MapObjectInstance& instance = objects.instances[index];
    std::string frames;
    for (const assets::MapSpriteFrame& frame : instance.frames) {
      frames += std::format("{}{}", frames.empty() ? "" : " ", frame.sprite);
    }
    stream << std::format("instance {}: at [{}, {}] flag {} sprites [{}]\n", index, instance.x,
                          instance.y, instance.flag, frames);
  }
}

// The ground point (y = 0) the map's camera shows at a background pixel, found by narrowing a grid
// around the camera's target: where to stand to see a sign. Nothing when no ground point lands
// within a few pixels of it.
std::optional<glm::vec2> groundUnder(const render::MapCamera& camera, glm::vec2 pixel) {
  constexpr int Reach = 16;
  constexpr int Rounds = 6;
  constexpr float FirstStep = 256.0F;
  constexpr float Narrowing = 4.0F;
  constexpr float Close = 3.0F;
  glm::vec2 centre{camera.desc().target.x, camera.desc().target.z};
  float best = std::numeric_limits<float>::max();
  float step = FirstStep;
  for (int round = 0; round < Rounds; ++round) {
    glm::vec2 found = centre;
    for (int dz = -Reach; dz <= Reach; ++dz) {
      for (int dx = -Reach; dx <= Reach; ++dx) {
        const glm::vec2 ground = centre + (glm::vec2{dx, dz} * step);
        const auto shown = camera.projectToBackground({ground.x, 0.0F, ground.y});
        if (!shown) {
          continue;
        }
        const float distance = glm::length(shown->pixel - pixel);
        if (distance < best) {
          best = distance;
          found = ground;
        }
      }
    }
    centre = found;
    step /= Narrowing;
  }
  return best <= Close ? std::optional{centre} : std::nullopt;
}

// Where each background sign is, and the place() that puts the player just below it (the original's
// axes: x as is, z negated), for a route or the console.
void writeSignPlaces(const std::filesystem::path& path, const render::MapCameraDesc& desc,
                     const signs::SignOverlay& overlay) {
  const render::MapCamera camera{desc};
  std::ofstream stream{path};
  stream
      << "# sign: region [x, y, width, height]; place(...) puts the player on the ground below it\n";
  for (const signs::Sign& sign : overlay.signs) {
    stream << std::format("{}: region [{}, {}, {}, {}]", sign.id, sign.region.x, sign.region.y,
                          sign.region.width, sign.region.height);
    if (sign.target.kind == signs::SignTarget::Kind::Background) {
      // Three steps below the sign, so the player does not stand in front of it; a sign high on a
      // wall shows over no ground there: the first ground below.
      constexpr int First = 3;
      constexpr int Steps = 14;
      constexpr float Down = 16.0F;
      const glm::vec2 bottom{static_cast<float>(sign.region.x) +
                                 (static_cast<float>(sign.region.width) / 2.0F),
                             static_cast<float>(sign.region.y + sign.region.height)};
      for (int step = First; step < Steps; ++step) {
        if (const auto ground =
                groundUnder(camera, bottom + glm::vec2{0.0F, Down * static_cast<float>(step)})) {
          stream << std::format(" place(\"player {} {} 0\")", std::lround(ground->x),
                                std::lround(-ground->y));
          break;
        }
      }
    }
    stream << "\n";
  }
}

} // namespace

int dumpSigns(const Options& options) {
  if (!options.shots) {
    core::logError("dump-signs: --shots <folder> says where the images go");
    return EXIT_FAILURE;
  }
  auto content = mountGameContent(options);
  if (!content) {
    core::logError("dump-signs: {}", content.error().message);
    return EXIT_FAILURE;
  }
  const assets::VirtualFileSystem& files = (*content)->files();
  const std::string language = options.language.value_or(std::string{DefaultLanguage});
  auto catalog = localization::Catalog::load(files, language);
  if (!catalog) {
    core::logError("dump-signs: {}", catalog.error().message);
    return EXIT_FAILURE;
  }
  std::error_code error;
  std::filesystem::create_directories(*options.shots, error);
  signs::MapSigns mapSigns{files};
  bool ok = true;
  for (const std::string& mapId : options.dumpSigns) {
    auto scene = render::loadMapSceneData(files, mapId);
    if (!scene) {
      core::logError("dump-signs: map {}: {}", mapId, scene.error().message);
      ok = false;
      continue;
    }
    const std::span<const assets::MapSprite> sprites =
        scene->document.objects ? std::span{scene->document.objects->sprites}
                                : std::span<const assets::MapSprite>{};
    const signs::SignOverlay overlay = mapSigns.overlay(mapId, language);
    ok = dumpImages(*options.shots, mapId, scene->images, sprites, overlay, true) && ok;
    writeSprites(*options.shots / std::format("{}-sprites.txt", mapId),
                 scene->document.objects.value_or(assets::MapObjects{}));
    writeSignPlaces(*options.shots / std::format("{}-signs.txt", mapId), scene->camera, overlay);
    const signs::PaintedSigns& painted = mapSigns.painted(
        scene->images, scene->document.objects ? &*scene->document.objects : nullptr, mapId,
        language, catalog->revision(), [&](std::string_view key) { return catalog->text(key); });
    if (!painted.patches.empty()) {
      assets::MapImages withSigns = scene->images;
      signs::applySignPatches(withSigns, painted.patches, sprites);
      ok = dumpImages(*options.shots, std::format("{}-{}", mapId, language), withSigns, sprites,
                      overlay, false) &&
           ok;
    }
    for (const std::string& warning : painted.warnings) {
      core::logWarning("dump-signs: {}: {}", mapId, warning);
    }
    core::logInfo("dump-signs: {} ({}): {} signs, {} patches, {} warnings", mapId, language,
                  overlay.signs.size(), painted.patches.size(), painted.warnings.size());
  }
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace dwe::app
