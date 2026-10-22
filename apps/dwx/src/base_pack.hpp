#pragma once

#include <dwe/core/error.hpp>
#include <dwe/formats/disc/iso9660.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "extractor.hpp"

namespace dwe::dwx {

// How much of a disc file the extraction covers.
enum class FileStatus : std::uint8_t {
  Extracted, // an extractor took it and understood all of it
  Partial,   // an extractor took it but reported unknowns
  Pending,   // no extractor takes it yet
};

// Returns the name of a status, as the coverage report writes it.
std::string_view fileStatusName(FileStatus status);

// The coverage of one disc file.
struct FileReport {
  formats::disc::IsoFile file;
  std::string sha1;
  std::string_view extractor; // empty when pending
  FileStatus status = FileStatus::Pending;
  std::vector<std::string> outputs; // paths inside the pack
  std::vector<std::string> unknowns;
  std::vector<std::string> notes;
};

// Everything one extraction made, in the volume's path order.
struct ExtractionRun {
  std::vector<FileReport> files;
  std::vector<OutputFile> outputs;
  // Outputs with the same extension and bytes, as groups of paths in output order; the same image
  // stored in several disc files gives one group.
  std::vector<std::vector<std::string>> duplicates;
};

// The totals of an extraction.
struct CoverageSummary {
  std::size_t files = 0;
  std::size_t extracted = 0;
  std::size_t partial = 0;
  std::size_t pending = 0;
  std::uint64_t bytes = 0;
  std::uint64_t bytesCovered = 0; // in files that are extracted or partial
  std::size_t outputs = 0;
  std::size_t duplicates = 0; // outputs that repeat an earlier one
};

// Hashes every file of the volume and runs the extractor that takes it, on several threads; the run
// is the same whatever the number of threads. An extractor that fails leaves its file partial, with
// the error as an unknown; two outputs with the same asset id, or one in the tool's own folder
// (dwx/), are an error of the tool.
core::Result<ExtractionRun> runExtractors(const Disc& disc, std::span<const Extractor> registry);

// Counts the files of each status and the bytes covered.
CoverageSummary summarize(const ExtractionRun& run);

// Writes the coverage report: the disc, the totals, per file its path, size, SHA-1, extractor,
// status, outputs, unknowns and notes, and the groups of duplicate outputs. The same run gives the
// same text.
core::Result<std::string> coverageYaml(const ExtractionRun& run, const DiscInfo& info);

// Whether dwx extract wrote the base pack in folder: it leaves a marker there.
[[nodiscard]] bool writtenByExtract(const std::filesystem::path& folder);

// Makes the output folder ready: creates it, or empties it if a previous extraction wrote it. A
// folder with other files is refused unless force is set, and then nothing in it is deleted.
core::Result<void> prepareOutputFolder(const std::filesystem::path& folder, bool force);

// Writes the base pack into a prepared folder: its manifest, every output, the coverage report and
// the marker that lets the next extraction replace it.
core::Result<void> writeBasePack(const std::filesystem::path& folder, const ExtractionRun& run,
                                 const DiscInfo& info);

// Mounts the written pack with the engine's virtual file system, checks that every output resolves
// to its file and opens every asset with the engine's readers (assets::checkContent). Returns how
// many assets the pack has.
core::Result<std::size_t> checkBasePack(const std::filesystem::path& folder,
                                        const ExtractionRun& run);

} // namespace dwe::dwx
