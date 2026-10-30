#include <dwe/assets/medal_table.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <format>
#include <span>
#include <utility>

#include "document_text.hpp"

namespace dwe::assets {

namespace {

using core::YamlNode;

constexpr std::string_view MedalsId = "data/medals";
constexpr std::string_view Context = "medal table";
constexpr std::string_view MedalsKey = "medals";
constexpr std::string_view IdKey = "id";
constexpr std::string_view IndexKey = "index";
constexpr std::string_view NameKey = "name";
constexpr std::string_view DescriptionKey = "description";

MedalEntry readMedal(YamlReader& reader, const YamlNode& node) {
  if (!reader.expectKeys(node, "a medal", {IdKey, IndexKey, NameKey, DescriptionKey})) {
    return {};
  }
  MedalEntry medal{.id = reader.text(reader.field(node, IdKey)),
                   .index = reader.integer<std::int32_t>(reader.field(node, IndexKey)),
                   .name = reader.text(reader.field(node, NameKey)),
                   .description = {}};
  const YamlNode& lines = reader.field(node, DescriptionKey);
  const std::span<const YamlNode> sequence = reader.sequence(lines, DescriptionKey);
  if (sequence.size() > medal.description.size()) {
    reader.fail(lines, std::format("medal {} has more than {} description lines", medal.index,
                                   medal.description.size()));
    return medal;
  }
  for (std::size_t i = 0; i < sequence.size(); ++i) {
    medal.description[i] = reader.text(sequence[i]);
  }
  return medal;
}

} // namespace

const MedalEntry* MedalTable::find(std::int32_t index) const {
  const auto found = std::ranges::find(medals, index, &MedalEntry::index);
  return found == medals.end() ? nullptr : &*found;
}

core::Result<MedalTable> parseMedalTable(std::string_view yaml) {
  auto tree = core::parseYaml(yaml);
  if (!tree) {
    return std::unexpected{core::Error{std::format("{}: {}", Context, tree.error().message)}};
  }
  YamlReader reader;
  const YamlNode& root = *tree;
  if (!reader.expectKeys(root, "the medal table", {MedalsKey})) {
    return std::unexpected{reader.error(Context)};
  }
  MedalTable table;
  const std::span<const YamlNode> medals =
      reader.sequence(reader.field(root, MedalsKey), MedalsKey);
  for (const YamlNode& medal : medals) {
    table.medals.push_back(readMedal(reader, medal));
  }
  for (std::size_t i = 0; reader.ok() && i < table.medals.size(); ++i) {
    for (std::size_t j = 0; j < i; ++j) {
      if (table.medals[i].index == table.medals[j].index) {
        reader.fail(medals[i], std::format("medal {} is listed twice", table.medals[i].index));
        break;
      }
    }
  }
  return reader.finish(std::move(table), Context);
}

core::Result<std::optional<MedalTable>> loadMedalTable(const VirtualFileSystem& vfs) {
  auto id = AssetId::parse(MedalsId);
  if (!id) {
    return std::unexpected{id.error()};
  }
  if (vfs.resolve(*id) == nullptr) {
    return std::optional<MedalTable>{};
  }
  auto text = readDocumentText(vfs, MedalsId);
  if (!text) {
    return std::unexpected{text.error()};
  }
  auto table = parseMedalTable(*text);
  if (!table) {
    return std::unexpected{table.error()};
  }
  return std::optional<MedalTable>{std::move(*table)};
}

} // namespace dwe::assets
