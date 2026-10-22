#pragma once

#include <dwe/assets/file_system.hpp>
#include <dwe/test_support/temp_folder.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

#include "base_pack.hpp"
#include "validate.hpp"

namespace dwe::dwx::testing {

// Writes the base pack of a run as dwx extract does and checks it as dwx validate does, with the
// schemas of content/schemas: what the engine cannot load or what breaks a schema, a reference or a
// contract. Returns a line
// "<file in the pack>: <message>" per problem. This is how CI checks, without the disc, that the
// engine loads what dwx extract writes.
inline std::string problemsOfWrittenPack(const ExtractionRun& run, const DiscInfo& info) {
  const test_support::TempFolder temp;
  const std::filesystem::path folder = temp.path() / "base";
  REQUIRE(prepareOutputFolder(folder, false).has_value());
  REQUIRE(writeBasePack(folder, run, info).has_value());
  REQUIRE(checkBasePack(folder, run).has_value());
  const auto report = validatePacks(assets::DiskFileSystem{}, folder, DWE_SCHEMA_DIR, {},
                                    assets::PackOrigin::Engine);
  REQUIRE(report.has_value());
  CHECK(report->assets > 0);
  std::string problems;
  for (const ValidationProblem& problem : report->problems) {
    problems += std::format(
        "{}: {}\n", std::filesystem::path{problem.file}.lexically_relative(folder).generic_string(),
        problem.message);
  }
  return problems;
}

// Every file under a folder, by its path relative to the folder, with its bytes.
inline std::map<std::string, std::string> readTree(const std::filesystem::path& root) {
  std::map<std::string, std::string> files;
  for (const auto& entry : std::filesystem::recursive_directory_iterator{root}) {
    if (entry.is_regular_file()) {
      std::ifstream stream{entry.path(), std::ios::binary};
      files[entry.path().lexically_relative(root).generic_string()] =
          std::string{std::istreambuf_iterator<char>{stream}, {}};
    }
  }
  return files;
}

} // namespace dwe::dwx::testing
