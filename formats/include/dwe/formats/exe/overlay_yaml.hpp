#pragma once

#include <dwe/formats/exe/overlay_tables.hpp>
#include <dwe/formats/exe/tables_yaml.hpp>

#include <string>

// Conversion of the overlay tables to YAML documents, one per overlay: fishing, stadium, versus and training. Each
// has a JSON Schema in content/schemas/<name>.schema.json. References to the executable's tables (items, maps,
// moves, Digimon types) become their ids, as in tablesToYaml.
namespace dwe::formats::exe {

TablesYaml fishingToYaml(const FishingTables& tables, const TableIds& ids);

// name is the document: "stadium" or "versus".
TablesYaml battleStageToYaml(const BattleStageTables& tables, const std::string& name);

TablesYaml trainingToYaml(const TrainingTables& tables, const TableIds& ids);

} // namespace dwe::formats::exe
