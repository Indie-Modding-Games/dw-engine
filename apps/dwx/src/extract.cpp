#include <dwe/formats/disc/boot.hpp>
#include <dwe/formats/disc/disc_image.hpp>
#include <dwe/formats/disc/file_table.hpp>
#include <dwe/formats/disc/iso9660.hpp>
#include <dwe/formats/disc/known_releases.hpp>

#include <algorithm>
#include <cstdio>
#include <format>
#include <optional>
#include <print>
#include <string>
#include <tuple>
#include <vector>

#include "dwx.hpp"

namespace dwe::dwx {

namespace {

using formats::disc::IsoFile;

// How a listed file is reached by the original.
enum class TableState : std::uint8_t {
  Listed,    // on the volume and in the executable's table
  Unlisted,  // on the volume only
  TableOnly, // in the table only: the directories hide it
  Unknown,   // the version is unknown, so there is no table to compare with
};

struct ListedFile {
  IsoFile file;
  TableState state;
};

std::string_view tableStateName(TableState state) {
  switch (state) {
  case TableState::Listed:
    return "yes";
  case TableState::Unlisted:
    return "no";
  case TableState::TableOnly:
    return "hidden";
  case TableState::Unknown:
    return "-";
  }
  return "?";
}

std::string_view layoutName(formats::disc::SectorLayout layout) {
  return layout == formats::disc::SectorLayout::Raw ? "raw 2352-byte sectors" : "2048-byte sectors";
}

bool isPathSearched(const IsoFile& file) {
  return std::ranges::find(formats::disc::pathSearchedFiles(), std::string_view{file.path}) !=
         formats::disc::pathSearchedFiles().end();
}

void reportImageDigest(const formats::disc::DiscImage& image) {
  const auto digest = hashFile(image.dataFile());
  if (!digest) {
    std::println("Image SHA-1: {}", digest.error().message);
    return;
  }
  const auto release = formats::disc::findKnownRelease(*digest);
  std::println("Image SHA-1: {} ({})", core::toHex(*digest),
               release ? release->description : std::string_view{"not a known image"});
}

} // namespace

int listDisc(const std::filesystem::path& path, bool verifyImage) {
  auto image = formats::disc::DiscImage::open(path);
  if (!image) {
    std::println(stderr, "dwx: {}", image.error().message);
    return ExitFailed;
  }
  std::println("Image: {} ({}, {} sectors)", image->dataFile().string(),
               layoutName(image->layout()), image->sectorCount());
  if (verifyImage) {
    reportImageDigest(*image);
  }

  const auto volume = formats::disc::readIsoVolume(*image);
  if (!volume) {
    std::println(stderr, "dwx: {}", volume.error().message);
    return ExitFailed;
  }
  std::println("Volume: {}, {} files", volume->volumeId, volume->files.size());

  const auto executable = formats::disc::findBootExecutable(*image, *volume);
  if (!executable) {
    std::println(stderr, "dwx: {}", executable.error().message);
    return ExitFailed;
  }
  const auto executableBytes = formats::disc::readIsoFile(*image, *executable);
  if (!executableBytes) {
    std::println(stderr, "dwx: {}", executableBytes.error().message);
    return ExitFailed;
  }
  const auto executableDigest = core::sha1(*executableBytes);
  const auto release = formats::disc::findKnownRelease(executableDigest);
  std::println("Executable: {} {} ({})", executable->path, core::toHex(executableDigest),
               release ? std::format("{}, version {}", release->description, release->version)
                       : std::string{"not a known release"});

  const auto location =
      release ? formats::disc::findFileTableLocation(release->version) : std::nullopt;
  std::vector<ListedFile> listed;
  std::vector<std::string> mismatches;
  std::vector<std::string> aliases;
  if (location) {
    const auto table = formats::disc::readFileTable(*executableBytes, *location);
    if (!table) {
      std::println(stderr, "dwx: file table: {}", table.error().message);
      return ExitFailed;
    }
    std::println("File table: {} entries at {:#010x}", table->size(), location->address);
    const auto check = formats::disc::checkFileTable(*table, volume->files);
    for (std::size_t i = 0; i < volume->files.size(); ++i) {
      listed.push_back({.file = volume->files[i],
                        .state = check.inTable[i] ? TableState::Listed : TableState::Unlisted});
    }
    for (const auto& entry : check.tableOnly) {
      listed.push_back({.file = {.path = entry.filename,
                                 .lba = static_cast<std::uint32_t>(entry.sector),
                                 .size = static_cast<std::uint32_t>(entry.size)},
                        .state = TableState::TableOnly});
    }
    mismatches = check.mismatches;
    aliases = check.aliases;
  } else {
    for (const auto& file : volume->files) {
      listed.push_back({.file = file, .state = TableState::Unknown});
    }
  }

  // Disc order, which is also the order of the table.
  std::ranges::sort(listed, {}, [](const ListedFile& entry) {
    return std::tie(entry.file.lba, entry.file.path);
  });
  std::println();
  std::println("{:>8} {:>10}  {:<40}  {:<6}  {}", "LBA", "SIZE", "SHA-1", "TABLE", "PATH");
  for (const auto& entry : listed) {
    if (std::uint64_t{entry.file.lba} + formats::disc::sectorSpan(entry.file) >
        image->sectorCount()) {
      std::println(stderr, "dwx: {} at sector {} lies outside the image", entry.file.path,
                   entry.file.lba);
      return ExitFailed;
    }
    const auto digest = formats::disc::hashIsoFile(*image, entry.file);
    if (!digest) {
      std::println(stderr, "dwx: {}", digest.error().message);
      return ExitFailed;
    }
    std::println("{:>8} {:>10}  {}  {:<6}  {}", entry.file.lba, entry.file.size,
                 core::toHex(*digest), tableStateName(entry.state), entry.file.path);
  }

  const auto count = [&](TableState state) {
    return std::ranges::count(listed, state, &ListedFile::state);
  };
  std::println();
  std::println("Files: {} ({} on the volume, {} hidden from the directories)", listed.size(),
               volume->files.size(), count(TableState::TableOnly));
  if (!location) {
    std::println("No file table to compare with: the version is unknown.");
    return ExitUnknown;
  }
  std::println("In the executable's table: {}; on the volume only: {}", count(TableState::Listed),
               count(TableState::Unlisted));
  for (const auto& entry : listed) {
    if (entry.state == TableState::Unlisted) {
      std::println("  outside the table: {}{}", entry.file.path,
                   isPathSearched(entry.file) ? " (opened by path, per the decomp)" : "");
    }
  }
  const auto searched = std::ranges::count_if(
      listed, [](const ListedFile& entry) { return isPathSearched(entry.file); });
  const auto searchedInTable = std::ranges::count_if(listed, [](const ListedFile& entry) {
    return isPathSearched(entry.file) && entry.state == TableState::Listed;
  });
  std::println(
      "Opened by path in the original (per the decomp): {} of {} on the disc, {} of them also in the table",
      searched, formats::disc::pathSearchedFiles().size(), searchedInTable);
  for (const auto& alias : aliases) {
    std::println("  table alias: {}", alias);
  }
  std::println("Mismatches with the table: {}", mismatches.size());
  for (const auto& mismatch : mismatches) {
    std::println("  {}", mismatch);
  }
  return mismatches.empty() ? ExitOk : ExitUnknown;
}

} // namespace dwe::dwx
