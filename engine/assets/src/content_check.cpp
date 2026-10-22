#include <dwe/assets/content_check.hpp>
#include <dwe/assets/digimon_table.hpp>
#include <dwe/assets/evolution_table.hpp>
#include <dwe/assets/image.hpp>
#include <dwe/assets/item_effect_table.hpp>
#include <dwe/assets/item_table.hpp>
#include <dwe/assets/map_document.hpp>
#include <dwe/assets/map_table.hpp>
#include <dwe/assets/media_headers.hpp>
#include <dwe/assets/model.hpp>
#include <dwe/assets/model_data.hpp>
#include <dwe/assets/move_table.hpp>
#include <dwe/assets/new_game_stats.hpp>
#include <dwe/assets/new_game_story.hpp>
#include <dwe/assets/png_only_images.hpp>
#include <dwe/assets/raise_table.hpp>
#include <dwe/assets/shop_table.hpp>
#include <dwe/assets/sound.hpp>
#include <dwe/assets/table_composition.hpp>
#include <dwe/assets/table_kinds.hpp>
#include <dwe/assets/text_table.hpp>
#include <dwe/assets/texture.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <format>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace dwe::assets {

namespace {

using Opener = core::Result<void> (*)(std::span<const std::byte>);

// The most threads the check uses. Assets are independent, so they are opened in parallel.
constexpr unsigned MaxThreads = 16;

// Turns the result of a reader into success or its error, dropping what it read.
template <auto Reader>
core::Result<void> discardValue(std::span<const std::byte> bytes) {
  if (auto read = Reader(bytes); !read) {
    return std::unexpected{read.error()};
  }
  return {};
}

// The item and move tables as a check reads them: the numbers the engine gave do not change what is
// checked.
core::Result<ItemTable> parseItems(std::string_view yaml) {
  return parseItemTable(yaml);
}

core::Result<MoveTable> parseMoves(std::string_view yaml) {
  return parseMoveTable(yaml);
}

core::Result<MapTable> parseMaps(std::string_view yaml) {
  return parseMapTable(yaml);
}

// Turns the result of a reader of text into success or its error, dropping what it read.
template <auto Reader>
core::Result<void> discardParsed(std::span<const std::byte> bytes) {
  if (auto read =
          Reader(std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()});
      !read) {
    return std::unexpected{read.error()};
  }
  return {};
}

// Transcodes a cooked texture to BC7, as the engine loads it on every GPU that samples BC7.
core::Result<void> openKtx2(std::span<const std::byte> bytes) {
  if (auto texture = decodeKtx2(bytes, TexelEncoding::Bc7); !texture) {
    return std::unexpected{texture.error()};
  }
  return {};
}

core::Result<void> openYaml(std::span<const std::byte> bytes) {
  const std::string_view text{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
  if (auto document = core::parseYaml(text); !document) {
    return std::unexpected{document.error()};
  }
  return {};
}

// Text read by a library (RmlUi documents and style sheets, Lua, licence notes): it must be UTF-8.
core::Result<void> openText(std::span<const std::byte> bytes) {
  std::size_t i = 0;
  while (i < bytes.size()) {
    const auto lead = static_cast<unsigned char>(bytes[i]);
    std::size_t length = 1;
    if (lead >= 0xF0U && lead <= 0xF4U) {
      length = 4;
    } else if (lead >= 0xE0U) {
      length = 3;
    } else if (lead >= 0xC2U && lead <= 0xDFU) {
      length = 2;
    } else if (lead >= 0x80U) {
      return core::fail(std::format("not UTF-8 text: byte {} starts no character", i));
    }
    if (lead >= 0xF5U || i + length > bytes.size()) {
      return core::fail(std::format("not UTF-8 text: byte {} starts no character", i));
    }
    for (std::size_t k = 1; k < length; ++k) {
      if ((static_cast<unsigned char>(bytes[i + k]) & 0xC0U) != 0x80U) {
        return core::fail(std::format("not UTF-8 text: byte {} starts no character", i));
      }
    }
    i += length;
  }
  return {};
}

// A TrueType or OpenType font, which the UI hands to FreeType: checks its signature.
core::Result<void> openFont(std::span<const std::byte> bytes) {
  constexpr std::array<std::array<unsigned char, 4>, 4> Signatures{{
      {0x00, 0x01, 0x00, 0x00},
      {'O', 'T', 'T', 'O'},
      {'t', 'r', 'u', 'e'},
      {'t', 't', 'c', 'f'},
  }};
  constexpr std::size_t HeaderSize = 12;
  if (bytes.size() >= HeaderSize) {
    for (const auto& signature : Signatures) {
      if (std::ranges::equal(bytes.first(4), signature, [](std::byte a, unsigned char b) {
            return static_cast<unsigned char>(a) == b;
          })) {
        return {};
      }
    }
  }
  return core::fail("not a TrueType or OpenType font");
}

constexpr std::array<std::pair<std::string_view, Opener>, 18> Openers = {{
    {"png", discardValue<decodePng>},
    {"ktx2", openKtx2},
    {"glb", discardValue<parseModel>},
    {"gltf", discardValue<readModel>},
    {"yaml", openYaml},
    {"wav", discardValue<readWavHeader>},
    {"opus", discardValue<decodeOpus>},
    {"ogg", discardValue<decodeOpus>},
    {"sf2", discardValue<readSoundFontHeader>},
    {"mid", discardValue<readMidiHeader>},
    {"y4m", discardValue<readY4mHeader>},
    {"rml", openText},
    {"rcss", openText},
    {"lua", openText},
    {"txt", openText},
    {"ttf", openFont},
    {"otf", openFont},
    {"ttc", openFont},
}};

// The YAML documents the game reads with a reader of their own, which is strict about their keys
// and values.
constexpr std::array<std::pair<std::string_view, Opener>, 12> DocumentReaders = {{
    {"map/*/data", discardParsed<parseMapDocument>},
    {"map/*/*/data", discardParsed<parseMapDocument>},
    {"data/digimon", discardParsed<parseDigimonTable>},
    {"data/raise", discardParsed<parseRaiseTable>},
    {"data/maps", discardParsed<parseMaps>},
    {"data/items", discardParsed<parseItems>},
    {"data/moves", discardParsed<parseMoves>},
    {"data/evolution", discardParsed<parseEvolutionTable>},
    {"data/item_effects", discardParsed<parseItemEffectTable>},
    {"data/shops", discardParsed<parseShopTable>},
    {"data/new_game_stats", discardParsed<parseNewGameStats>},
    {"data/new_game_story", discardParsed<parseNewGameStory>},
}};

// The strict reader of each kind of content table (table_kinds.hpp) but the Digimon table's, by its
// role.
constexpr std::array<std::pair<std::string_view, Opener>, 5> TableReaders = {{
    {"raise", discardParsed<parseRaiseTable>},
    {"maps", discardParsed<parseMaps>},
    {"items", discardParsed<parseItems>},
    {"moves", discardParsed<parseMoves>},
    {"evolution", discardParsed<parseEvolutionTable>},
}};

// Opens an asset as the game reads it: an image the game reads from its PNG with that reader, else
// by its id.
core::Result<void> openAssetAsGameReads(const AssetId& id, std::string_view extension,
                                        std::span<const std::byte> bytes) {
  // TODO(W10b): pass the image's _meta once a declaration in it can exempt the image, read from the
  // same pack as the image so another pack's metadata cannot exempt it.
  if (const std::optional<PngReading> reading = pngReadingOf(id, nullptr)) {
    return openPngOnlyImage(*reading, extension, bytes);
  }
  return openAsset(id, extension, bytes);
}

using ComposedTables = std::vector<std::pair<AssetId, const TableKind*>>;

const TableKind* kindOf(const ComposedTables& tables, const AssetId& id) {
  const auto found = std::ranges::find(
      tables, id, [](const auto& table) -> const AssetId& { return table.first; });
  return found == tables.end() ? nullptr : found->second;
}

// The bytes of an asset as the game reads them: a content table composed entry by entry, else the
// file that wins.
core::Result<std::vector<std::byte>> readAsGameReads(const VirtualFileSystem& fileSystem,
                                                     const AssetId& id, const TableKind* kind) {
  if (kind == nullptr) {
    return fileSystem.read(id);
  }
  auto text = readComposedTable(fileSystem, id, *kind->composition);
  if (!text) {
    return std::unexpected{text.error()};
  }
  const auto bytes = std::as_bytes(std::span{*text});
  return std::vector<std::byte>{bytes.begin(), bytes.end()};
}

// Opens a content table with the strict reader of its kind, whatever id a ruleset gave it; a
// Digimon table's new Digimon must have their model contracts.
core::Result<void> openTable(const VirtualFileSystem& fileSystem, const TableKind& kind,
                             std::span<const std::byte> bytes) {
  if (auto parsed = openYaml(bytes); !parsed) {
    return parsed;
  }
  if (kind.composition == &DigimonComposition) {
    auto table = parseDigimonTable(
        std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()});
    if (!table) {
      return std::unexpected{table.error()};
    }
    return requireNewSpeciesContracts(fileSystem, *table);
  }
  const auto reader =
      std::ranges::find(TableReaders, kind.role, [](const auto& entry) { return entry.first; });
  return reader == TableReaders.end() ? core::Result<void>{} : reader->second(bytes);
}

} // namespace

core::Result<void> openAsset(std::string_view extension, std::span<const std::byte> bytes) {
  for (const auto& [known, opener] : Openers) {
    if (known == extension) {
      return opener(bytes);
    }
  }
  return core::fail(std::format("the engine has no reader for .{} files", extension));
}

core::Result<void> openAsset(const AssetId& id, std::string_view extension,
                             std::span<const std::byte> bytes) {
  if (auto opened = openAsset(extension, bytes); !opened || extension != "yaml") {
    return opened;
  }
  if (const std::optional<TextTableName> table = textTableOf(id)) {
    if (auto texts = parseTextTable(
            *table, std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()});
        !texts) {
      return std::unexpected{texts.error()};
    }
    return {};
  }
  for (const auto& [pattern, reader] : DocumentReaders) {
    if (matchesIdPattern(pattern, id)) {
      return reader(bytes);
    }
  }
  return {};
}

std::vector<ContentProblem> checkContent(const VirtualFileSystem& fileSystem) {
  const std::vector<AssetId> ids = fileSystem.ids();
  const ComposedTables tables = composedTablesOf(fileSystem);
  std::vector<std::optional<ContentProblem>> found(ids.size());
  std::atomic<std::size_t> next{0};
  const auto worker = [&] {
    for (std::size_t i = next++; i < ids.size(); i = next++) {
      const AssetSource* source = fileSystem.resolve(ids[i]);
      const TableKind* kind = kindOf(tables, ids[i]);
      auto bytes = readAsGameReads(fileSystem, ids[i], kind);
      if (!bytes) {
        found[i] = ContentProblem{
            .id = ids[i], .file = pathText(source->file), .message = bytes.error().message};
        continue;
      }
      const std::string extension = source->file.extension().string();
      const auto opened =
          kind != nullptr
              ? openTable(fileSystem, *kind, *bytes)
              : openAssetAsGameReads(
                    ids[i], std::string_view{extension}.substr(extension.empty() ? 0 : 1), *bytes);
      if (!opened) {
        found[i] = ContentProblem{
            .id = ids[i], .file = pathText(source->file), .message = opened.error().message};
      }
    }
  };
  const unsigned count = std::clamp(std::thread::hardware_concurrency(), 1U, MaxThreads);
  std::vector<std::thread> threads;
  for (unsigned i = 1; i < count; ++i) {
    threads.emplace_back(worker);
  }
  worker();
  for (auto& thread : threads) {
    thread.join();
  }
  std::vector<ContentProblem> problems;
  for (auto& problem : found) {
    if (problem) {
      problems.push_back(std::move(*problem));
    }
  }
  return problems;
}

} // namespace dwe::assets
