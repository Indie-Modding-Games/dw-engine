#include "export_images.hpp"

#include <dwe/assets/file_system.hpp>
#include <dwe/assets/image.hpp>
#include <dwe/assets/image_palettes.hpp>
#include <dwe/assets/image_replacement.hpp>
#include <dwe/assets/indexed_image.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>

#include <cstdint>
#include <format>
#include <print>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "dwx.hpp"
#include "messages.hpp"

namespace dwe::dwx {

namespace {

constexpr std::string_view PngExtension = ".png";
constexpr std::string_view MapPrefix = "map/";
constexpr std::string_view BackgroundPrefix = "background_";
constexpr std::string_view TexturePrefix = "texture/";
constexpr std::string_view PartialSuffix = ".partial";
// The text that translates the map names, and the key of each name in it (map_names.0001).
constexpr std::string_view TextFolder = "text/";
constexpr std::string_view MainText = "/main";
constexpr std::string_view MapNameKey = "map_names.{:04}";

std::string_view textOf(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::string_view trimmed(std::string_view text) {
  const std::size_t first = text.find_first_not_of(' ');
  if (first == std::string_view::npos) {
    return {};
  }
  return text.substr(first, text.find_last_not_of(' ') - first + 1);
}

bool isBackground(std::string_view id) {
  if (!id.starts_with(MapPrefix)) {
    return false;
  }
  const std::size_t slash = id.rfind('/');
  return slash > MapPrefix.size() && id.substr(slash + 1).starts_with(BackgroundPrefix);
}

// The base pack's copy of an image: replacements are measured against it.
const assets::AssetSource* basePng(const assets::VirtualFileSystem& files,
                                   const assets::AssetId& id) {
  const auto sources = files.sources(id);
  if (sources.empty() || assets::pathText(sources.front().file.extension()) != PngExtension) {
    return nullptr;
  }
  return &sources.front();
}

core::Result<void> writeText(const std::filesystem::path& file, std::string_view text) {
  return writeFile(file, std::as_bytes(std::span{text}));
}

// The image's palettes as colours, from its metadata; none when it has no metadata or palettes.
core::Result<std::vector<std::vector<assets::PaletteColor>>>
paletteColors(const assets::VirtualFileSystem& files, const assets::AssetId& image) {
  auto metaId = assets::AssetId::parse(image.str() + std::string{assets::ImageMetaSuffix});
  if (!metaId) {
    return std::unexpected{metaId.error()};
  }
  std::vector<std::vector<assets::PaletteColor>> colors;
  if (files.resolve(*metaId) == nullptr) {
    return colors;
  }
  auto meta = files.read(*metaId);
  if (!meta) {
    return std::unexpected{meta.error()};
  }
  auto palettes = assets::parseImagePalettes(textOf(*meta));
  if (!palettes) {
    return std::unexpected{palettes.error()};
  }
  for (const std::vector<std::uint16_t>& palette : *palettes) {
    std::vector<assets::PaletteColor>& converted = colors.emplace_back();
    for (const std::uint16_t color : palette) {
      converted.push_back(assets::imageColorFromOriginal(color));
    }
  }
  return colors;
}

core::Result<void> writePaletteImage(const assets::AssetId& image, std::size_t palette,
                                     std::span<const std::byte> png,
                                     const std::filesystem::path& folder) {
  auto id = assets::paletteImageId(image, palette);
  if (!id) {
    return std::unexpected{id.error()};
  }
  return writeFile(folder / (id->str() + std::string{PngExtension}), png);
}

// Writes an image of texture/ once per palette; returns how many files it wrote.
core::Result<std::size_t> exportTexture(const assets::VirtualFileSystem& files,
                                        const assets::AssetId& id,
                                        const assets::AssetSource& source,
                                        const std::filesystem::path& folder) {
  auto bytes = files.read(source);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  auto palettes = paletteColors(files, id);
  if (!palettes) {
    return std::unexpected{palettes.error()};
  }
  if (palettes->empty()) {
    if (auto written = writePaletteImage(id, 0, *bytes, folder); !written) {
      return std::unexpected{written.error()};
    }
    return 1;
  }
  auto indexed = assets::decodeIndexedPng(*bytes);
  if (!indexed) {
    return core::fail(
        core::formatMessage(messages::ExportNotIndexed, id.str(), indexed.error().message));
  }
  for (std::size_t palette = 0; palette < palettes->size(); ++palette) {
    auto png = assets::encodePng(assets::applyPalette(*indexed, palettes->at(palette)));
    if (!png) {
      return std::unexpected{png.error()};
    }
    if (auto written = writePaletteImage(id, palette, *png, folder); !written) {
      return std::unexpected{written.error()};
    }
  }
  return palettes->size();
}

// The map names of the language's main text, when the packs have it.
std::optional<core::YamlNode> translatedText(const assets::VirtualFileSystem& files,
                                             const std::string& language) {
  auto id = assets::AssetId::parse(std::string{TextFolder} + language + std::string{MainText});
  if (!id || files.resolve(*id) == nullptr) {
    return std::nullopt;
  }
  auto bytes = files.read(*id);
  if (!bytes) {
    return std::nullopt;
  }
  auto text = core::parseYaml(textOf(*bytes));
  if (!text || !text->isMapping()) {
    return std::nullopt;
  }
  return std::move(*text);
}

core::Result<std::size_t> writeMapNames(const assets::VirtualFileSystem& files,
                                        const std::string& language,
                                        const std::filesystem::path& folder) {
  auto table = assets::loadMapTable(files);
  if (!table) {
    return std::unexpected{table.error()};
  }
  const std::optional<core::YamlNode> translated = translatedText(files, language);
  std::string lines;
  for (const assets::MapEntry& map : table->maps) {
    std::string_view name = table->displayName(map);
    if (translated) {
      const core::YamlNode* text = translated->find(std::format(MapNameKey, map.loadingName));
      if (text != nullptr && text->isScalar() && !trimmed(text->text()).empty()) {
        name = trimmed(text->text());
      }
    }
    lines += std::format("{}\t{}\n", map.id, name);
  }
  if (auto written = writeText(folder / ExportedMapsFile, lines); !written) {
    return std::unexpected{written.error()};
  }
  return table->maps.size();
}

core::Result<ExportSummary> writeExport(const assets::VirtualFileSystem& files,
                                        const std::string& language,
                                        const std::filesystem::path& folder) {
  ExportSummary summary;
  auto maps = writeMapNames(files, language, folder);
  if (!maps) {
    return std::unexpected{maps.error()};
  }
  summary.maps = *maps;
  for (const assets::AssetId& id : files.ids()) {
    const assets::AssetSource* source = basePng(files, id);
    if (source == nullptr) {
      continue;
    }
    if (isBackground(id.str())) {
      auto bytes = files.read(*source);
      if (!bytes) {
        return std::unexpected{bytes.error()};
      }
      if (auto written = writeFile(folder / (id.str() + std::string{PngExtension}), *bytes);
          !written) {
        return std::unexpected{written.error()};
      }
      ++summary.backgrounds;
    } else if (id.str().starts_with(TexturePrefix)) {
      auto written = exportTexture(files, id, *source, folder);
      if (!written) {
        return std::unexpected{written.error()};
      }
      summary.images += *written;
    }
  }
  return summary;
}

// Whether a folder may be replaced: it does not exist, is empty, or holds an export.
bool replaceable(const std::filesystem::path& folder) {
  std::error_code error;
  if (!std::filesystem::exists(folder, error)) {
    return true;
  }
  return std::filesystem::is_directory(folder, error) &&
         (std::filesystem::is_empty(folder, error) ||
          std::filesystem::is_regular_file(folder / ExportedMapsFile, error));
}

} // namespace

core::Result<ExportSummary> exportImages(std::span<const std::filesystem::path> folders,
                                         const std::filesystem::path& out,
                                         const std::string& language) {
  const assets::DiskFileSystem disk;
  std::vector<assets::FoundPack> found;
  for (const std::filesystem::path& folder : folders) {
    auto packs = assets::findPacks(disk, folder);
    if (!packs) {
      return std::unexpected{packs.error()};
    }
    found.insert(found.end(), packs->packs.begin(), packs->packs.end());
  }
  auto opened = assets::openPacks(disk, found, assets::PackOrigin::Engine);
  if (!opened) {
    return std::unexpected{opened.error()};
  }
  auto files = assets::VirtualFileSystem::mount(opened->locations);
  if (!files) {
    return std::unexpected{files.error()};
  }
  std::filesystem::path partial = out;
  partial += PartialSuffix;
  if (!replaceable(out) || !replaceable(partial)) {
    return core::fail(core::formatMessage(messages::ExportOutputTaken, assets::pathText(out)));
  }
  std::error_code error;
  std::filesystem::remove_all(partial, error);
  auto summary = writeExport(*files, language, partial);
  if (summary) {
    std::filesystem::remove_all(out, error);
    if (!error) {
      std::filesystem::rename(partial, out, error);
    }
    if (error) {
      summary = core::fail(core::formatMessage(messages::ExportCannotReplace, assets::pathText(out),
                                               error.message()));
    }
  }
  if (!summary) {
    std::filesystem::remove_all(partial, error);
  }
  return summary;
}

int exportImagesCommand(std::span<const std::filesystem::path> folders,
                        const std::filesystem::path& out, const std::string& language) {
  auto summary = exportImages(folders, out, language);
  if (!summary) {
    std::println(stderr, "dwx: {}", summary.error().message);
    return ExitFailed;
  }
  std::println("Exported images: {}", assets::pathText(out));
  std::println("Map backgrounds: {}, images by palette: {}, maps named: {}", summary->backgrounds,
               summary->images, summary->maps);
  return ExitOk;
}

} // namespace dwe::dwx
