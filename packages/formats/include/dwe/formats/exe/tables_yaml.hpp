#pragma once

#include <dwe/core/yaml.hpp>
#include <dwe/formats/exe/tables.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

// Conversion of the executable's tables to the YAML documents of content/base. Each document has a
// JSON Schema in content/schemas/<name>.schema.json. Records that other data refers to get a string
// id; their original index is kept as a field. Same tables, same text.
namespace dwe::formats::exe {

// The string ids of the records other tables refer to, by original index.
struct TableIds {
  std::vector<std::string>
      digimon; // from the model file name (PTR_DIGIMON_FILE_NAMES), which is unique
  std::vector<std::string> moves;
  std::vector<std::string> items;
  std::vector<std::string> maps; // from the MAP file name
  std::vector<std::string> tournaments;
  std::vector<std::string> medals;
  std::vector<std::string> bgmTracks;
};

// A YAML document and the name of its file and schema.
struct TableDocument {
  std::string name;
  core::YamlNode document;
};

struct TablesYaml {
  std::vector<TableDocument> documents;
  // What a reader of the extraction report should know: texts that kept escaped bytes, references
  // outside the table they point to.
  std::vector<std::string> notes;
};

// Writes a text of the executable so that it survives as YAML: printable ASCII as it is, a
// backslash as "\\" and any other byte as "\xNN". The original bytes can be rebuilt from the
// result.
std::string escapeGameText(std::string_view raw);

// Turns names into ids: lowercase ASCII letters and digits, any other run of bytes as one '_'. An
// empty result gets
// "<prefix>_<index>"; a repeated one gets "_<index>" appended, so every id is unique and stable for
// the same input.
std::vector<std::string> makeIds(std::span<const std::string> names, std::string_view prefix);

// Builds the ids of every table that others refer to.
TableIds makeTableIds(const ExecutableTables& tables);

// Converts every table to its document: digimon, skeletons, moves, items, item_effects, evolution,
// raise, cards, tournaments, medals, music, shops and maps.
TablesYaml tablesToYaml(const ExecutableTables& tables);

} // namespace dwe::formats::exe
