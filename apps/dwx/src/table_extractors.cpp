#include "table_extractors.hpp"

#include <dwe/core/yaml.hpp>
#include <dwe/formats/exe/tables_yaml.hpp>

#include <algorithm>
#include <format>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "asset_names.hpp"
#include "text_extractors.hpp"

namespace dwe::dwx {

namespace {

using core::YamlNode;

constexpr std::string_view BootConfigPath = "SYSTEM.CNF";
constexpr std::string_view Blanks = " \t\r";

bool isUpper(char c) {
  return c >= 'A' && c <= 'Z';
}

bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

std::string_view trim(std::string_view text) {
  const auto first = text.find_first_not_of(Blanks);
  if (first == std::string_view::npos) {
    return {};
  }
  return text.substr(first, text.find_last_not_of(Blanks) - first + 1);
}

core::Result<OutputFile> yamlOutput(const core::Result<assets::AssetId>& id, const YamlNode& node) {
  if (!id) {
    return std::unexpected{id.error()};
  }
  const auto text = core::emitYaml(node);
  if (!text) {
    return std::unexpected{text.error()};
  }
  const auto bytes = std::as_bytes(std::span{*text});
  return OutputFile{
      .id = *id, .extension = "yaml", .bytes = std::vector<std::byte>(bytes.begin(), bytes.end())};
}

// Notes the MAP_ENTRIES records whose file is not on the disc; the original never loads those maps.
void noteMissingMaps(const Disc& disc, const GameTables& tables, std::vector<std::string>& notes) {
  const auto& entries = tables.tables.mapEntries;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    if (entries[i].filename.empty()) {
      continue;
    }
    const std::string path = mapVolumePath(i, entries[i].filename, ".MAP");
    if (disc.findFile(path) == nullptr) {
      notes.push_back(std::format("MAP_ENTRIES[{}] ({}) names {}, which is not on the disc", i,
                                  tables.ids.maps[i], path));
    }
  }
}

} // namespace

bool handlesExecutable(std::string_view path) {
  // Four letters, '_', three digits, '.' and two digits, at the root: SLUS_010.32.
  return path.size() == 11 && std::ranges::all_of(path.substr(0, 4), isUpper) && path[4] == '_' &&
         std::ranges::all_of(path.substr(5, 3), isDigit) && path[8] == '.' &&
         std::ranges::all_of(path.substr(9), isDigit);
}

core::Result<Extraction> extractExecutable(const Disc& disc, const formats::disc::IsoFile& file) {
  if (file.path != disc.info().executablePath) {
    return core::fail("not the executable that SYSTEM.CNF boots");
  }
  const GameTables* tables = disc.tables();
  if (tables == nullptr) {
    return core::fail("the tables of this version are not known; nothing is read");
  }
  const auto yaml = formats::exe::tablesToYaml(tables->tables);
  Extraction extraction;
  std::string documents;
  for (const auto& document : yaml.documents) {
    auto output = yamlOutput(names::tableDocument(document.name), document.document);
    if (!output) {
      return std::unexpected{output.error()};
    }
    extraction.outputs.push_back(std::move(*output));
    documents += (documents.empty() ? "" : ", ") + document.name;
  }
  extraction.notes.emplace_back(
      "only the data tables are taken, with the strings and the font; the code and the "
      "rest of the data are not assets");
  extraction.notes.push_back(std::format("tables: {}", documents));
  extraction.notes.insert(extraction.notes.end(), yaml.notes.begin(), yaml.notes.end());
  noteMissingMaps(disc, *tables, extraction.notes);
  if (auto text = addExecutableText(disc, file, extraction); !text) {
    return std::unexpected{text.error()};
  }
  return extraction;
}

bool handlesBootConfig(std::string_view path) {
  return path == BootConfigPath;
}

core::Result<Extraction> extractBootConfig(const Disc& disc, const formats::disc::IsoFile& file) {
  const auto bytes = formats::disc::readIsoFile(disc.source(), file);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  const std::string text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
  Extraction extraction;
  YamlNode settings = YamlNode::mapping();
  std::size_t lineNumber = 0;
  for (std::string_view rest = text; !rest.empty();) {
    const std::size_t end = std::min(rest.find('\n'), rest.size());
    const std::string_view line = trim(rest.substr(0, end));
    rest.remove_prefix(std::min(end + 1, rest.size()));
    ++lineNumber;
    if (line.empty()) {
      continue;
    }
    const std::size_t equals = line.find('=');
    const std::string key =
        equals == std::string_view::npos ? "" : names::segment(trim(line.substr(0, equals)));
    if (key.empty()) {
      extraction.unknowns.push_back(std::format("line {} is not KEY = VALUE", lineNumber));
      continue;
    }
    settings.set(key, YamlNode::scalar(formats::exe::escapeGameText(trim(line.substr(equals + 1))),
                                       YamlNode::ScalarStyle::Quoted));
  }
  auto output = yamlOutput(names::bootConfig(), settings);
  if (!output) {
    return std::unexpected{output.error()};
  }
  extraction.outputs.push_back(std::move(*output));
  return extraction;
}

} // namespace dwe::dwx
